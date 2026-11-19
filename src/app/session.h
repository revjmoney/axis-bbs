// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// One caller: their line, their Node, and whatever they are in (native LORD, an ANSI movie or pack, a download or upload),
// or on a board not yet set up, the setup gate and wizard. The program makes one when a call comes in, calls service()
// each loop turn, and drops it once done() (a Session never deletes itself: hangUp only lets go of what it holds). The board
// and the PC run callers through this same code; what is the program's own comes in through Services (app/services.h).
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "app/host.h"
#include "app/services.h"
#include "bbs/ansiviewer.h"
#include "bbs/node.h"
#include "bbs/setup_gate.h"
#include "bbs/setup_wizard.h"
#include "bbs/transferjob.h"
#include "door/hostfs.h"
#include "lord/game.h"
#include "net/telnetstream.h"

namespace axis::app {

class Session {
public:
    // The line is ready to go (made by the program, its own settings set): this takes its link reports when modem_link is on,
    // holds a slow caller's output in it, and sends the telnet negotiation.
    Session(int slot, std::unique_ptr<net::TelnetStream> line, Services& s);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // The first screen: a Node with every hook (or the setup gate on a board not set up). console: the sysop at the panel,
    // logged in with no password, the WFC first.
    void open(bool console = false);
    void wireNode();  // every hook a Node gets (open calls it)
    // The caller's keys, through the Node (or the setup gate and wizard). service() calls it at its place in the turn.
    AXIS_NOINLINE void input(uint32_t nowMs);
    // One loop turn: the link report, the status line, the line's backlog, then the transfer, LORD, the movie, the bridge
    // to another board, or the keys and the Node's poll.
    AXIS_NOINLINE void service(uint32_t nowMs);
    // The caller is gone (or sent away): everything let go, the line parked or closed. `why` is not logged (the board's
    // "hang up" line is as before).
    AXIS_NOINLINE void hangUp(const char* why = nullptr);
    AXIS_NOINLINE void startDoor();  // the door the Node asked for: native LORD or the ANSI viewer
    AXIS_NOINLINE void finishLord();
    AXIS_NOINLINE void finishViewer();
    bool done() const { return !line; }
    int  nodeNumber() const;
    void sendApp(const std::string& s);  // a turn's output to the caller

    const int slot;
    // (members die in reverse: the line last; lordFs and lordLocks before lord, so a LORD still in a script closes its file)
    std::unique_ptr<net::TelnetStream>      line;
    std::unique_ptr<bbs::Node>              node;
    std::unique_ptr<bbs::SetupGate>         gate;
    std::unique_ptr<bbs::SetupWizard>       wizard;
    uint32_t                                startMs = 0;
    std::string                             lastStatus;  // the last status line sent to the cluster ("handle|activity")
    std::unique_ptr<door::HostFs>           lordFs;
    std::unique_ptr<door::LockTable>        lordLocks;
    std::unique_ptr<axis::lord::NativeLord> lord;
    std::unique_ptr<bbs::AnsiViewer>        viewer;
    std::unique_ptr<bbs::TransferJob>       job;
    uint32_t                                doorStartMs = 0;
    uint32_t                                jobStartMs = 0, jobDrawnMs = 0;

private:
    // LORD's mail: the seen list for this caller (MAILGOT in the game's folder) and the message base.
    struct LordMail {
        Services*           s = nullptr;
        std::string         me;
        door::HostFs*       fs = nullptr;
        int                 seen[48] = {};
        int                 nseen = 0;
        bool                loaded = false;
    };
    static void lordLoadSeen(LordMail* box);
    static void lordSaveSeen(LordMail* box);
    static int  lordPull(void* ctx, char* body, int bodyN, char* from, int fromN);
    static void lordPost(void* ctx, const char* from, const char* to, const char* body);

    void dropLord();
    void releaseDoorSlot();
    void abortDoor(bbs::Node::DoorEnd how);
    void startDoorInner();
    std::string areaName(const std::string& id) const;
    const char* scopeName(const char* what);
    AXIS_NOINLINE void callerInput(const std::string& app);
    AXIS_NOINLINE void takeLinkReport();
    AXIS_NOINLINE void postStatus();
    AXIS_NOINLINE void serviceJob();
    void dirty() { if (s_.dirty) s_.dirty(); }
    void doing(const std::string& what) { if (s_.doing) s_.doing(what); }
    void zipHeap() { if (s_.zipHeap) s_.zipHeap(); }
    void doorNote(bbs::DoorDiag d) { if (s_.doorNote) s_.doorNote(d); }
    void romLog(const char* line) { if (s_.romLog) s_.romLog(line); }
    void nap(uint32_t ms) { if (s_.nap) s_.nap(ms); }
    bool follower() const { return s_.follower && s_.follower(); }

    Services& s_;
    LordMail  mail_;
};

}  // namespace axis::app
