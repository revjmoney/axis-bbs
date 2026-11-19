// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// What a caller's Session (app/session.h) uses from the program around it: the shared stores, the settings, chat between
// nodes, and the program's own hooks. The board (main.cpp) fills it with its cluster-aware stores and its board-only
// callbacks (the panel, the LED, the radio link report, the Axis Net bridge, heap notes); the PC app fills the local
// versions and may leave every board-only hook empty. Required (a Node or the setup calls them unchecked, so an empty
// one throws or crashes): sysInfo, rng, saveUsers. Every other hook may be empty: it then does nothing, or what its
// comment names.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/boarddata.h"
#include "app/host.h"
#include "app/stats.h"
#include "app/themeprovider.h"
#include "bbs/config.h"
#include "bbs/diag.h"
#include "bbs/filelib.h"
#include "bbs/msgbase.h"
#include "bbs/node.h"
#include "bbs/users.h"
#include "bbs/wfc.h"
#include "door/hostfs.h"
#include "net/telnet.h"
#include "net/telnetstream.h"

namespace axis::bbs {
class SetupWizard;
class TransferJob;
}  // namespace axis::bbs

namespace axis::app {

class Session;

struct Services {
    Services(Host& h, BoardData& d, ThemeProvider& t, Stats& st, bbs::UserStore& u, const std::shared_ptr<const bbs::BbsConfig>& c)
        : host(h), data(d), theme(t), stats(st), users(u), cfg(c) {}

    Host&                 host;
    BoardData&            data;
    ThemeProvider&        theme;
    Stats&                stats;
    bbs::UserStore&       users;
    std::function<void()> saveUsers;  // required
    // The board's settings, always the current copy: a reference to the program's own pointer, which a change replaces.
    const std::shared_ptr<const bbs::BbsConfig>& cfg;
    std::function<std::string(const std::string&)> sysopRun;
    // A chat line or a page from the session in `slot` (its handle, the text), to every node.
    std::function<void(int slot, const std::string& handle, const std::string& text, bool page)> chat;
    std::function<std::vector<bbs::Node::WhoLine>()> who;
    std::function<bbs::MessageStore&()> messages;  // empty: the message base is offline (LORD mail is dropped)
    std::function<bbs::FileLibrary&()> files;  // empty: no file areas (no library; movies and transfers end at once)

    // the rest of what a Node is given
    uint32_t (*rng)() = nullptr;                                          // required: the random numbers (passwords, setup)
    std::function<uint32_t()> unixNow;                                    // 0 when the clock is not set
    std::function<std::shared_ptr<const bbs::BbsConfig>()> nodeCfg;      // the settings a new Node gets (empty: cfg)
    std::function<bbs::SysInfo()> sysInfo;                                // required: the Node reads it on many screens
    std::function<bbs::WfcSnapshot()> wfcSnapshot;
    std::function<bool(const std::string&)> warrior;                      // registration's LORD warrior (empty: none)
    // Notes, broadcasts and split chat keys from the session in `slot`: kind, target node (0: all), its handle, the text.
    std::function<void(int slot, int kind, int to, const std::string& handle, const std::string& text)> nodeLink;
    std::function<int(int slot)> nodeNumber;                              // the node a slot answers as (empty: slot + 1)
    std::function<void(Session&)> netFiles;                               // Net Files on this session's Node (board only)
    std::function<bool()> follower;                                       // a cluster follower (empty: never)

    // the setup over telnet (a board that is not set up yet)
    std::function<bool()> ready;                                          // callers get a Node (empty: cfg->setupDone)
    std::function<std::string()> setupPin;
    std::function<void()> setupChosen;                                    // the right PIN: setup is over telnet now
    std::function<void(bbs::SetupWizard&)> setupSave;                     // the wizard finished: keep its settings (empty: "Nothing was saved")
    std::function<void()> setupFinished;                                  // after the goodbye: open for callers
    std::function<std::string()> dialName;                                // "axis.local", for the goodbye and AXISBBS.NFO

    // doors
    // A door's folder for `id` into fs (own: a lock table only this door uses); the lock table the door uses.
    std::function<door::LockTable*(const std::string& id, std::unique_ptr<door::HostFs>& fs, std::unique_ptr<door::LockTable>& own)>
        doorFiles;
    std::function<void(int node)> releaseDoor;                            // the program's own door slot, given back
    std::function<void(bbs::DoorDiag)> doorNote;                          // the last door, for @STATUS
    std::function<void()> zipHeap;                                        // the inflate window was taken or given back

    // transfers and Net Files (board only)
    std::function<void(Session&)> drawTransfer;                           // the transfer window on the board's screen
    std::function<bbs::TransferJob*(Session&)> netJob;                    // a Net Files download's job (nullptr: stopped)
    std::function<void(const void* owner)> netRelease;                    // a Net Files fetch this caller holds
    std::function<void(const void* job, std::string& note)> netJobNote;  // why a Net Files download stopped
    std::function<void(const std::string& area, const std::string& name, const std::string& desc, const std::string& who)>
        netUploaded;

    // the call
    std::function<void(Session&)> lineEnding;                             // the line is about to go: lockouts, radio calls
    std::function<void(std::unique_ptr<net::TelnetStream>)> park;          // a goodbye still going out (empty: closed now)
    std::function<void(const std::string& handle)> callerGone;            // the last caller
    std::function<void(Session&, const net::LinkInfo&, bool first)> radio;  // the modem's link report, for the WFC
    std::function<bool()> clustered;                                      // post status lines (empty: never)
    std::function<void(int slot, const std::string& handle, const std::string& activity)> status;
    std::function<void()> dirty;                                          // the board's screen should be drawn again
    std::function<void(const std::string&)> doing;                        // what the board is busy with (crash notes)
    std::function<std::string()> heapText;                                // " [free/largest KB]" for the hang up line
    std::function<void(const char*)> romLog;                              // a line when the heap may be gone (bad_alloc)
    std::function<void(uint32_t ms)> nap;                                 // the pause after a door or movie step

    // the Axis Net bridge (board only)
    std::function<bool(Session&)> axisPump;                               // true: a call to another board owns the keys
    std::function<void(Session&)> axisCall;                               // open the call the node asked for (empty: "Could not reach")
    std::function<bool()> axisBusy;                                       // a call is already on (this board has one line out)
    std::function<bool()> axisDirty;                                      // the Axis Net list changed this turn
    std::function<void()> axisProbe;                                      // the node asked to speak to the Axis Net
};

}  // namespace axis::app
