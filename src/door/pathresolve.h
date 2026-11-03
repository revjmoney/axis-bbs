// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// DOS paths ("C:\LORD\X.DAT", any case) to real paths inside the door folder, on stores that are
// case-sensitive (LittleFS) or not (FAT, NTFS). Every HostFs uses this, so all behave alike.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace axis {
namespace door {

// cwd: the door's current directory ("" = its root, parts separated by '\').
// list(dir): names inside a host folder ("" = the door root, parts separated by '/'); empty if none.
// Returns the host path relative to the door root, parts separated by '/', matching existing names
// in any case and keeping the asked case for new ones. "" if the path is the root itself or has a
// part containing ':' (which could leave the door folder on some hosts).
std::string resolveDosPath(const std::string& cwd, const std::string& dosPath,
                           const std::function<std::vector<std::string>(const std::string&)>& list);

}  // namespace door
}  // namespace axis
