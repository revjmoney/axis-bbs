// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The axis program in a cluster of boards: which BBS it is at boot (paths.h's Profile), its standing in the cluster
// (<data>/cluster/state), and the cluster key (<data>/cluster.key, mode 0600, the key alone, never logged). The
// boot decision runs before setup(); clusterHostPoll runs from every turn after loop().
#pragma once
#include <cstdint>
#include <string>

#include "platform/linux/paths.h"

// What this program last was in the cluster. <data>/cluster/state holds three lines:
// state=follower|main|handedback, epoch=<the main's epoch>, since=<seconds since 1970>.
enum class Standing { Follower, Main, HandedBack };
struct ClusterState {
    Standing state = Standing::Follower;
    uint32_t epoch = 0;
    long     since = 0;
};
bool clusterStateLoad(ClusterState& s);        // false: no state file (or it cannot be read); `s` keeps the defaults
bool clusterStateSave(const ClusterState& s);  // a temp file, then a rename

// The cluster key: "" when unset. Read, written and removed only here (Preferences' "clu"/"key" comes here too).
std::string clusterKeyRead();
bool        clusterKeyWrite(const std::string& key);  // mode 0600, a temp file, then a rename
bool        clusterKeyRemove();                        // true when a key file was there and is gone

// Before setup(): state main (and a key) is MirrorMain; state handedback is Follower; else listen on UDP 2325 up to
// listenMs for an announcement that opens with the key: version 2 is Follower, older logs the "older firmware"
// line; nothing heard (or no key to open one with) is Solo.
Profile decideProfile(uint32_t listenMs = 5000);
void    clusterHostStart(Profile p);       // sets Cluster::defaultHost for the profile
void    clusterHostPoll(uint32_t nowMs);   // from turn(), after loop()
