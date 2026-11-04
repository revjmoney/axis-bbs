// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// One caller's session: welcome, handle, main menu, logoff. Portable: it sees clean
// keystrokes (after telnet) and produces bytes to send; the host owns the socket.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "bbs/config.h"
#include "bbs/filebrowser.h"
#include "bbs/filelib.h"
#include "bbs/msgreader.h"
#include "bbs/newuser.h"
#include "bbs/splash.h"
#include "bbs/users.h"
#include "bbs/version.h"

namespace axis::bbs {

struct SysInfo {
    std::string ip, host, wifi, timeText, storage;
    std::string hardware = "ESP32";
    int         rssi = 0;
    uint32_t    uptimeSec = 0, freeHeap = 0, callsToday = 0;
    int         nodesTotal = 1;
    std::vector<std::string> nodes;  // who is on each node ("" = waiting); this caller's own node too
};

class Node {
public:
    enum class State { Splash, Login, Password, ApplyAsk, Applying, Pending, OneLinerAsk, OneLiner, Menu, Messages, Doors, InDoor, Files, Transfer, Chat, Paging, Done };
    struct WhoLine {
        int         node = 0;
        std::string handle, activity;  // handle "" = waiting for a caller
    };
    enum class DoorEnd { Normal, OutOfMemory, CarrierLost, TimeLimit, LoadError, Busy, NotHere };  // NotHere: asked for at the board's own screen (no terminal for a door)  // Busy: another caller here has the slot
    static constexpr size_t   kMaxHandle = 30;

    // files: the file areas' store (nullptr: the Files menu says the areas are offline)
    Node(int nodeNumber, std::function<SysInfo()> info, BbsConfig cfg = {}, FileLibrary* files = nullptr);
    // accounts: without them anyone is let in by handle (the first builds, tests)
    void setAccounts(UserStore* users, std::function<void()> save, uint32_t (*rng)(), std::function<uint32_t()> unixNow);
    bool isSysop() const { return sysop_; }
    // Chat across the nodes: say(text, page) sends a teleconference line (or a page for the sysop);
    // who() lists every node. Without them [T] [W] [P] say the chat is offline.
    void setChat(std::function<void(const std::string&, bool)> say, std::function<std::vector<WhoLine>()> who) {
        say_ = std::move(say);
        who_ = std::move(who);
    }
    // the message base ([M]): nullptr = offline
    void setMessages(MessageStore* store) { msgStore_ = store; }
    // the oneliner wall: get() the newest lines ("handle: text"), add() one; shown after login
    void setOneLiners(std::function<std::vector<std::string>()> get, std::function<void(const std::string&)> add) {
        linersGet_ = std::move(get);
        linersAdd_ = std::move(add);
    }
    // a line said on any node (this one's own lines come back this way too, so all see one order)
    void chatIn(int node, const std::string& handle, const std::string& text, bool page);
    // Big fixed screens (the login art) go straight to the caller through this, a piece at a time
    // from flash, instead of through takeOutput(): a copy of them needs more heap than a busy board has.
    void        setDirect(std::function<void(const char*, size_t)> direct) { direct_ = std::move(direct); }
    void        begin(uint32_t nowMs);
    void        beginLocal(uint32_t nowMs);  // the sysop at the board itself: logged in as sysop, no splash
    void        input(const uint8_t* p, size_t n, uint32_t nowMs);
    void        poll(uint32_t nowMs);
    std::string takeOutput();
    bool        wantsHangup() const { return state_ == State::Done; }
    State       state() const { return state_; }
    std::string activityText() const;  // what the caller is doing, in a few words ("files: ANSI Packs")
    const std::string& handle() const { return handle_; }
    // the door the caller picked ("" unless InDoor); the host runs it, then calls resumeAfterDoor
    std::string      doorRequested() const { return state_ == State::InDoor ? cfg_.doors[door_].id : ""; }
    const DoorEntry& door() const { return cfg_.doors[door_]; }
    void             resumeAfterDoor(DoorEnd how, uint32_t nowMs);
    // a download or upload from the Files menu (State::Transfer): the host runs it, then reports back
    const TransferRequest& transfer() const { return browser_->transfer(); }
    void transferDone(bool ok, uint32_t bytes, uint32_t ms, const std::string& note, uint32_t nowMs);

private:
    void key(char c);
    void showMenu();
    void showMainMenu();  // the full-screen menu: the drippy MENU banner and every command
    void showDoors();
    void loggedIn();
    void showPending();
    void pendingMenu();
    void prompt();
    void showWelcome();
    void menuOrLiners();  // after login: the oneliner wall if there is one, then the menu
    void lineKey(char c);
    std::unique_ptr<Splash> splash_;  // the RotatoCAM splash, before the welcome  // line input for the teleconference and pages
    int                       node_;
    std::function<void(const std::string&, bool)> say_;
    std::function<std::vector<WhoLine>()>        who_;
    std::function<std::vector<std::string>()>    linersGet_;
    MessageStore*                                msgStore_ = nullptr;
    std::unique_ptr<MessageReader>               msgs_;
    std::map<std::string, int>                   lastRead_;  // without an account (tests, first builds)
    std::function<void(const std::string&)>      linersAdd_;
    std::function<SysInfo()>  info_;
    BbsConfig                 cfg_;
    State                     state_ = State::Login;
    std::function<void(const char*, size_t)> direct_;
    std::string               out_, line_, handle_;
    uint32_t                  lastInput_ = 0;
    size_t                    door_ = 0;
    UserStore*                users_ = nullptr;
    std::function<void()>     saveUsers_;
    uint32_t (*rng_)() = nullptr;
    std::function<uint32_t()> unixNow_;
    int                       userId_ = 0, tries_ = 0;
    bool                      sysop_ = false, wantSysop_ = false;
    std::unique_ptr<NewUserApp> app_;
    FileLibrary*              files_;
    std::unique_ptr<FileBrowser> browser_;
};

// Sent to a caller who connects while the node is in use.
std::string busyScreen();

}  // namespace axis::bbs
