// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// One caller's session: welcome, handle, main menu, logoff. Portable: it sees clean
// keystrokes (after telnet) and produces bytes to send; the host owns the socket.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "bbs/config.h"
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
    enum class State { Login, Menu, Doors, InDoor, Done };
    enum class DoorEnd { Normal, OutOfMemory, CarrierLost, TimeLimit, LoadError };
    static constexpr size_t   kMaxHandle = 30;

    Node(int nodeNumber, std::function<SysInfo()> info, BbsConfig cfg = {});
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

private:
    void key(char c);
    void showMenu();
    void showDoors();
    void prompt();
    int                       node_;
    std::function<SysInfo()>  info_;
    BbsConfig                 cfg_;
    State                     state_ = State::Login;
    std::string               out_, line_, handle_;
    uint32_t                  lastInput_ = 0;
    size_t                    door_ = 0;
};

// Sent to a caller who connects while the node is in use.
std::string busyScreen();

}  // namespace axis::bbs
