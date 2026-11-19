// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The axis program in a cluster of boards: which BBS it is at boot (paths.h's Profile), its standing in the cluster
// (<data>/cluster/state), and the cluster key (<data>/cluster.key, mode 0600, the key alone, never logged). The
// boot decision runs before setup(); clusterHostPoll runs from every turn after loop().
#pragma once
#include <cstdint>
#include <string>

#include "platform/linux/paths.h"

// What this program last was in the cluster. <data>/cluster/state holds four lines:
// state=follower|main|handedback, epoch=<the main's epoch>, since=<seconds since 1970>, and
// solo_until=<seconds since 1970>: after the follower's watchdog gave up on a main, solo until then (0: no back-off).
enum class Standing { Follower, Main, HandedBack };
struct ClusterState {
    Standing state = Standing::Follower;
    uint32_t epoch = 0;
    long     since = 0;
    long     soloUntil = 0;
};
bool clusterStateLoad(ClusterState& s);        // false: no state file (or it cannot be read); `s` keeps the defaults
bool clusterStateSave(const ClusterState& s);  // a temp file, then a rename

// The cluster key: "" when unset. Read, written and removed only here (Preferences' "clu"/"key" comes here too).
std::string clusterKeyRead();
bool        clusterKeyWrite(const std::string& key);  // mode 0600, a temp file, then a rename
bool        clusterKeyRemove();                        // true when a key file was there and is gone

// Before setup(): state main (and a key) is MirrorMain; state handedback is Follower; a solo_until still ahead is
// Solo at once (the back-off after a failed join, logged); else listen on UDP 2325 up to
// listenMs for an announcement that opens with the key: version 2 is Follower, older logs the "older firmware"
// line; nothing heard (or no key to open one with) is Solo.
Profile decideProfile(uint32_t listenMs = 5000);
// Before setup(): the profile's cluster rules (Cluster::defaultHost). Follower: holds a mirror, never main by itself,
// joins only a version 2 main, and a Yield asks takeoverWanted(). MirrorMain: holds a mirror and claims main at once
// at the saved epoch. Solo: the boards' own rules (and the link and cluster ports are silent, WiFiUdp.h).
void clusterHostStart(Profile p);

// From turn(), after loop(), and the only place that restarts the program (hostRestart runs turn() itself while it
// hangs up, so a restart asked from inside loop(), a command or a cluster hook only sets a flag this acts on).
// Solo: the drain once the solo watch has heard a version 2 main. Follower: the watchdog (no main for
// no_main_seconds) and the hand-back's end. Terminal mode: WFC once the board is set up (clusterHostWaitSetup).
void clusterHostPoll(uint32_t nowMs);

// Solo, port 2325 (WiFiUdp.h): one LAN datagram the cluster's port heard. It is opened with the cluster key and an
// announcement is noted; the drain itself runs from clusterHostPoll.
void soloWatchHeard(const std::string& datagram, uint32_t fromIp);

// Terminal mode in a cluster profile with a board that is not set up yet: the main's Settings frame sets it up, so
// clusterHostPoll checks every 2 s and types WFC once it is.
void clusterHostWaitSetup();

// A Yield came (Cluster::HostHooks::onYield, inside the cluster's tick): true takes main now. Not yet: it logs and
// declines (the card mirror's takeover replaces this).
bool takeoverWanted();
