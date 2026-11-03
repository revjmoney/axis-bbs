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
#include "bbs/newuser.h"
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
    enum class State { Login, Password, ApplyAsk, Applying, Pending, Menu, Doors, InDoor, Files, Transfer, Done };
    enum class DoorEnd { Normal, OutOfMemory, CarrierLost, TimeLimit, LoadError };
    static constexpr size_t   kMaxHandle = 30;

    // files: the file areas' store (nullptr: the Files menu says the areas are offline)
    Node(int nodeNumber, std::function<SysInfo()> info, BbsConfig cfg = {}, FileLibrary* files = nullptr);
    // accounts: without them anyone is let in by handle (the first builds, tests)
    void setAccounts(UserStore* users, std::function<void()> save, uint32_t (*rng)(), std::function<uint32_t()> unixNow);
    bool isSysop() const { return sysop_; }
    void        begin(uint32_t nowMs);
    void        input(const uint8_t* p, size_t n, uint32_t nowMs);
    void        poll(uint32_t nowMs);
    std::string takeOutput();
    bool        wantsHangup() const { return state_ == State::Done; }
    State       state() const { return state_; }
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
    void showDoors();
    void loggedIn();
    void showPending();
    void pendingMenu();
    void prompt();
    int                       node_;
    std::function<SysInfo()>  info_;
    BbsConfig                 cfg_;
    State                     state_ = State::Login;
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
