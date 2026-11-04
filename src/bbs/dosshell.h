// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// DOS Prompt, a door built into the firmware: a plain C:\> prompt over a sandbox folder on the SD
// card (dosbox), behind its own password. Every path goes through the door file system, so ".."
// stops at the sandbox root and other drives don't exist: nothing of the BBS can be reached.
// Three wrong passwords end the visit; five in a row lock the door for everyone for ten minutes.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "door/bytestream.h"
#include "door/hostfs.h"

namespace axis::bbs {

struct DosGate {  // board-wide: survives each visit to the door
    int      fails = 0;
    uint32_t lockedUntil = 0;
};

struct DosShellConfig {
    std::string               handle;      // who is at the prompt (for the log)
    std::string               salt, hash;  // the door password (password.h); empty = door disabled
    uint64_t                  quotaBytes = 16u * 1024 * 1024;  // the sandbox can't grow past this
    std::function<uint32_t()> clockMs;
    std::function<std::string()> timeText;  // for DATE and TIME ("" = unknown)
};

class DosShell {
public:
    static constexpr int      kTries = 3, kLockFails = 5;
    static constexpr uint32_t kLockMs = 10u * 60 * 1000;

    DosShell(door::HostFs& fs, door::ByteStream& line, DosGate& gate, DosShellConfig cfg);
    void begin();  // ask for the password (or say why not)
    bool step();   // handle waiting keys; false once the caller leaves (or hangs up)
    std::vector<std::string>& events() { return events_; }  // for the board's log; the caller takes them

private:
    enum class St { Password, Prompt, More, Done };
    void     send(const std::string& s) { line_.write((const uint8_t*)s.data(), s.size()); }
    void     prompt();
    void     password(const std::string& typed);
    void     command(const std::string& typed);
    void     dir(const std::string& arg);
    void     type(const std::string& arg);
    void     copy(const std::string& from, const std::string& to);
    void     cd(const std::string& arg);
    uint64_t used(const std::string& dir);  // bytes in the sandbox under dir
    void     error(int code);

    door::HostFs&            fs_;
    door::ByteStream&        line_;
    DosGate&                 gate_;
    DosShellConfig           cfg_;
    St                       st_ = St::Done;
    int                      tries_ = 0;
    std::string              typed_;
    std::vector<std::string> history_;          // DOSKEY: the commands typed, newest last
    size_t                   histAt_ = 0;
    int                      esc_ = 0;          // an arrow key's ESC sequence so far
    int                      moreH_ = -1;       // MORE: the open file
    std::string              moreBuf_;          // what's read but not shown yet
    void                     moreNext();
    void                     tree(const std::string& dir, const std::string& lead);
    void                     listArc(const std::string& arg);
    void                     recall(const std::string& cmd);
    std::vector<std::string> events_;
};

}  // namespace axis::bbs
