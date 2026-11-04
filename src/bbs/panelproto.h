// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The sysop control panel's side of a board (LAN port 2326): one line per message.
//   board: AXISPANEL 1 <nonce>          app: SALT <handle>      board: SALT <salt> (or "-")
//   app:   AUTH <handle> <sha256(nonce + salted hash)>         board: OK / NO (3rd NO: BYE)
//   app:   CMD <@command>                board: its output, then END
// After OK the board also pushes EVT <event log line> and STAT <key=value ...>. Only the sysop
// account or a user at security 255 gets in; the password itself never crosses the wire.
#pragma once
#include <functional>
#include <string>

#include "bbs/config.h"
#include "bbs/users.h"

namespace axis::bbs {

class PanelSession {
public:
    PanelSession(const BbsConfig& cfg, const UserStore& users, std::string nonce);
    std::string greeting() const { return "AXISPANEL 1 " + nonce_ + "\n"; }
    std::string line(const std::string& in);  // what to send back
    bool        authed() const { return authed_; }
    bool        closed() const { return closed_; }
    std::string who() const { return who_; }
    std::function<std::string(const std::string&)> run;      // runs an @ command, returns its output
    std::function<void(const std::string&)>        onEvent;  // for the event log

private:
    bool lookup(const std::string& handle, std::string& salt, std::string& hash, std::string& name) const;

    const BbsConfig& cfg_;
    const UserStore& users_;
    std::string      nonce_, who_;
    bool             authed_ = false, closed_ = false;
    int              bad_ = 0;
};

}  // namespace axis::bbs
