// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// One caller's session: welcome, handle, main menu, logoff. Portable: it sees clean
// keystrokes (after telnet) and produces bytes to send; the host owns the socket.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "bbs/config.h"

namespace axis::bbs {

struct SysInfo {
    std::string ip, host, wifi, timeText, storage;
    int         rssi = 0;
    uint32_t    uptimeSec = 0, freeHeap = 0;
};

class Node {
public:
    enum class State { Login, Menu, Done };
    static constexpr size_t   kMaxHandle = 30;

    Node(int nodeNumber, std::function<SysInfo()> info, BbsConfig cfg = {});
    void        begin(uint32_t nowMs);
    void        input(const uint8_t* p, size_t n, uint32_t nowMs);
    void        poll(uint32_t nowMs);
    std::string takeOutput();
    bool        wantsHangup() const { return state_ == State::Done; }
    State       state() const { return state_; }
    const std::string& handle() const { return handle_; }

private:
    void key(char c);
    void showMenu();
    void prompt();
    int                       node_;
    std::function<SysInfo()>  info_;
    BbsConfig                 cfg_;
    State                     state_ = State::Login;
    std::string               out_, line_, handle_;
    uint32_t                  lastInput_ = 0;
};

// Sent to a caller who connects while the node is in use.
std::string busyScreen();

}  // namespace axis::bbs
