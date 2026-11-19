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

// A Yield came (Cluster::HostHooks::onYield, inside the cluster's tick). Always false: in state handedback, or with the
// mirror's first copy not finished, it logs and declines; otherwise it starts the takeover (the callers are held off,
// and clusterHostPoll restarts as the mirror's main once nobody is on, at most takeover_wait_seconds later).
bool takeoverWanted();

// The card mirror (mirrorhost.cpp), Follower only. mirrorPoll runs from clusterHostPoll, never inside a request, and
// makes at most one request a turn: the first full copy, the live follow, the noon check and the compare passes.
void mirrorPoll(uint32_t nowMs);
bool mirrorComplete();         // the manifest says the first full copy is finished
void mirrorCheckSoon();        // a compare pass at the next chance
bool mirrorSaveForTakeover();  // writes the manifest now and copies it to <data>/mirror.takeover

// What @CARDMIRROR shows of the mirror (mirrorhost.cpp; the manifest is read once if it was not yet).
struct MirrorStatus {
    bool        complete = false;  // the first full copy is finished
    size_t      files = 0;         // in the manifest
    uint64_t    bytes = 0;
    std::string noon;              // the day of the last noon check ("" none yet)
    long        lastCheckAt = 0;   // when the last pass ended in this run (seconds since 1970, 0: none)
    std::string lastCheck;         // what it found
    std::string pass;              // the pass running now ("" none)
    size_t      pending = 0;       // live follow: changed paths waiting to be copied
    size_t      verifying = 0;     // live follow: copies checked again until they settle
    long        lastErrAt = 0;
    std::string lastErr;           // "" none in this run
    bool        stopped = false;   // disk full: copying waits
};
MirrorStatus mirrorStatus();

// The hand-back (handback.cpp), MirrorMain only.
// takeoverNote: fs_remap.cpp calls it after every successful write-type call on a "/sd/..." path in MirrorMain (the
// same calls as the board's cardwatch.cpp). The card path is kept in a set and, when new, appended to
// <data>/mirror.changes (one a line, read back at the start). mirrorSaveForTakeover clears that file.
void takeoverNote(const char* boardPath);
// From clusterHostPoll, MirrorMain only, never inside itself: at most one request a turn while a hand-back runs,
// paced like the mirror; its end sends 'I E', saves state handedback and restarts (hostRestart).
void handbackPoll(uint32_t nowMs);
// For @HANDBACK (nativecmd.cpp): the lines it prints; GO only sets the hand-back up (handbackPoll runs it).
void handbackShow(std::string& out);
bool handbackGo(const std::string& node, std::string& out);  // node: "" (the one live card board) or a node number
bool handbackStop(std::string& out);
size_t handbackChangeCount();
