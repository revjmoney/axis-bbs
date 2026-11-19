// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// axispc's BBS: the host the shared code runs on (PcHost) and everything around the callers (PcBbs): the settings and the
// user base in <data>/flash, the message base and the file areas on <data>/sd, the telnet ports, the radio line, and one
// Session per caller (app/session.h), all served from one event loop thread. What the board does in main.cpp, without the
// board: no screen, no Wi-Fi, no cluster (standalone: Services' remote and follower hooks stay empty).
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "app/boarddata.h"
#include "app/commands.h"
#include "app/host.h"
#include "app/services.h"
#include "app/session.h"
#include "app/stats.h"
#include "app/themeprovider.h"
#include "bbs/config.h"
#include "bbs/diag.h"
#include "bbs/eventlog.h"
#include "bbs/loginguard.h"
#include "bbs/msgbase.h"
#include "bbs/node.h"
#include "bbs/rotatelog.h"
#include "bbs/users.h"
#include "bbs/wfc.h"
#include "door/hostfs.h"
#include "net/telnet.h"
#include "net/telnetstream.h"
#include "pcapp/listener.h"
#include "platform/cyd/sdfiles.h"

namespace axis::pcapp {

// The shared code's host (app/host.h): the steady clock, std::random_device, the event log (<data>/flash/events.log, the
// board's 8 KB rotation) and the data folder's sd/ and flash/.
class PcHost : public app::Host {
public:
    explicit PcHost(const std::string& dataDir);
    uint32_t    nowMs() override;
    uint32_t    unixNow() override;
    uint32_t    random32() override;
    void        event(const std::string& line) override;  // "HH:MM line" into the log screen and file, "axis: line" on stdout
    std::string sdRoot() override { return sd_; }
    bool        hasSd() override { return true; }
    uint64_t    sdFreeBytes() override;
    std::string flashRoot() override { return flash_; }

    void                         openEventFile();  // the Logs screen picks up where the last run left off
    void                         clearEvents();    // @LOGCLEAR's part: the screen's lines and both files
    const bbs::EventLog&         events() const { return events_; }

private:
    std::string                 sd_, flash_;
    std::chrono::steady_clock::time_point start_;
    std::random_device          rd_;
    bbs::EventLog               events_;
    std::unique_ptr<bbs::RotatingLog> file_;
};

class PcBbs {
public:
    explicit PcBbs(const std::string& dataDir);
    ~PcBbs();
    PcBbs(const PcBbs&) = delete;
    PcBbs& operator=(const PcBbs&) = delete;

    // The settings, the users and the rest from the data folder; the telnet ports and the radio line (modemPath: --modem,
    // else axis.conf's modem=). False (with why) when no telnet port opened. hostConfLoad must have run.
    bool start(const std::string& modemPath, std::string& why);
    // One turn of the event loop: new callers, the radio line, every caller's turn, the parked goodbyes. True when
    // anything moved (a caller came or went, bytes went either way), so the loop naps only when idle.
    bool turn();
    // SIGTERM, SIGINT: every caller hung up, the radio line hung up, the users and the stats saved.
    void stop();

    bool               setUp() const { return cfg_->setupDone && !inSetup_; }  // callers get a Node
    const std::string& setupPin() const { return setupPin_; }
    uint16_t           lanPort() const { return listener_.lanPort(); }
    PcHost&            host() { return host_; }

    // The terminal's sysop console (console.h): a local Node with every hook a caller's gets, on the WFC. Chat, pages and
    // broadcasts reach it too while it is set (null: none).
    std::unique_ptr<bbs::Node> makeLocalNode();
    void                       setConsoleNode(bbs::Node* n) { console_ = n; }

    // One @ command (the sysop menu's, the console's): axispc's own (@NODES @STATS @LOG @LOGCLEAR @STATUS), then the
    // portable ones (app/commands.h). Nothing written: not a command known here.
    void        runCommand(const std::string& cmd, app::Out& out);
    std::string sysopRun(const std::string& cmd);  // its reply as text, cut at 6 KB as the board's

private:
    struct Parked {
        std::unique_ptr<net::TelnetStream> line;
        uint32_t                           sinceMs = 0;
    };
    struct CountingMsgs;

    void wireServices();
    void answer(int fd, uint32_t ip, bool gated);
    int  freeSlot() const;
    int  callersOnline() const;
    void deliverChat(int fromNode, const std::string& handle, const std::string& text, bool page, int kind, int to);
    void saveUsers();
    bool loadConfig();
    bool saveConfig();
    void cfgSet(bbs::BbsConfig c);
    bbs::BbsConfig& cfgEdit();
    void pushToCallers(const std::shared_ptr<const bbs::BbsConfig>& prev);
    void makeAreaFolders();
    bool writeWarrior(const std::string& handle);
    std::string lordFolderId() const;
    bbs::SysInfo      sysInfo();
    bbs::WfcSnapshot  wfcSnapshot();
    std::string       lanAddress();
    std::string       timeText() const;
    std::string       dialName() const { return cfg_->boardName + ".local"; }
    bbs::MessageStore& messages();
    void              drainParked(uint32_t now);
    bool              moved();

    std::string                              dataDir_;
    PcHost                                   host_;
    app::BoardData                           data_;
    app::ThemeProvider                       theme_;
    app::Stats                               stats_;
    bbs::UserStore                           users_;
    std::shared_ptr<const bbs::BbsConfig>    cfg_;
    app::Services                            services_;
    // (before the callers: a caller's Node, game and line use these until it goes, and members die in reverse)
    Listener                                 listener_;
    bbs::LoginGuard                          loginGuard_;
    std::unique_ptr<CountingMsgs>            msgs_;
    std::unique_ptr<SdFileLibrary>           files_;
    door::LockTable                          doorLocks_;
    bbs::Node*                               console_ = nullptr;
    bbs::DoorDiag                            lastDoor_ = bbs::DoorDiag::None;
    net::LinkInfo                            lastRadio_;
    std::string                              lastRadioWho_;
    uint32_t                                 lastRadioMs_ = 0;
    std::string                              lastCaller_ = "none yet";
    std::string                              pageText_;
    std::string                              setupPin_;
    bool                                     inSetup_ = false;  // a caller passed the PIN: setup is over telnet now
    size_t                                   pendingSeen_ = 0;
    uint32_t                                 lastRollMs_ = 0;
    uint64_t                                 bytesSeen_ = 0;    // every line's bytes in and out at the last turn
    std::string                              lanIp_;
    uint32_t                                 lanIpMs_ = 0;
    bool                                     haveLanIp_ = false;
    std::string                              id_;  // a locally administered address made from the machine id (@STATUS id=)
    std::vector<std::string>                 activity_;    // each slot's "handle|Messages", ... (Session::postStatus)
    std::vector<int>                         radioFds_;    // radio calls the modem line handed over, answered next turn
    std::vector<Parked>                      parked_;
    std::vector<std::unique_ptr<app::Session>> sessions_;  // null: a free slot (node = slot + 1)
};

}  // namespace axis::pcapp
