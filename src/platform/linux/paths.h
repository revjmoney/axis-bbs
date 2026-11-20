// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The data folder, and how the board's paths land in it. The board keeps its card at /sd and its internal flash at
// /littlefs; here they are <data>/sd and <data>/flash, with the NVS namespaces in <data>/prefs and logs in <data>/log.
// fs_remap.cpp sends every path the board's code hands to the C library through hostPath(), so main.cpp and the core
// keep their own paths unchanged.
#pragma once
#include <string>

// The data folder, absolute, no trailing slash ("" until axisDataSetup succeeded).
std::string axisDataDir();

// Which BBS this program is, set once before setup() (clusterhost.h decides it) and never changed while it runs:
// Solo keeps plan 1's folders (sd/, flash/, prefs/); the two cluster profiles share cluster/flash and cluster/prefs,
// so the solo users and settings stay out of the cluster's and the cluster's out of solo. A Follower has no card
// (its /sd fails, as a cardless board's); the MirrorMain's card is the mirror (<data>/mirror).
enum class Profile { Solo, Follower, MirrorMain };
void    axisSetProfile(Profile p);
Profile axisProfile();
const char* axisProfileName(Profile p);  // "solo", "follower", "mirror main"

// Where the NVS namespaces live for the profile: <data>/prefs or <data>/cluster/prefs ("" with no data folder).
std::string axisPrefsDir();

// The board's paths for the profile, anything else unchanged:
//   "/sd..."       Solo <data>/sd...     Follower "" (no card)          MirrorMain <data>/mirror...
//   "/littlefs..." Solo <data>/flash...  Follower <data>/cluster/flash... MirrorMain <data>/cluster/flash...
// Only the exact folder or a path under it counts ("/sdcard" is left alone). A board path asked for before the data folder is set maps to
// "" so the call fails (ENOENT) instead of touching the real /sd or /littlefs.
std::string hostPath(const char* boardPath);

// Makes the data folder ready: `requested` from --data, or "" for the default (/var/lib/axis when the program runs
// as user axis, else ~/.axis; on Windows, %LOCALAPPDATA%\axis, else %USERPROFILE%\.axis). Creates it (mode 0700 on
// Linux; the user's normal permissions on Windows) with sd/, flash/, prefs/, log/, cluster/flash/,
// cluster/prefs/ and mirror/; if `seed` is given and
// sd/ is empty, copies that folder (a board card's contents) into sd/. On failure says why in `why` and returns false.
bool axisDataSetup(const std::string& requested, const std::string& seed, std::string& why);
