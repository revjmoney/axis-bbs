// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// One door run: load the program, then slice the CPU, tick the timer, idle when the door waits,
// and stop on exit, out of memory, lost carrier or the time limit.
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "door/bios.h"
#include "door/bytestream.h"
#include "door/cpu.h"
#include "door/dos.h"
#include "door/dosmem.h"
#include "door/dropfile.h"
#include "door/fossil.h"
#include "door/guestmem.h"
#include "door/hostfs.h"
#include "door/imagesource.h"

namespace axis {
namespace door {

enum class DoorResult { Running, Exited, OutOfMemory, CarrierLost, TimeLimit, LoadError };
enum class DropFile { None, DoorSys, Dorinfo };

struct DoorConfig {
    std::string           dir;            // the door's folder (informational; drive C: is the HostFs root)
    std::string           exe;            // e.g. "LORD.EXE"
    std::string           args;           // command tail, e.g. "0 NOEMS /DREW"
    size_t                ramBudget = 128 * 1024;
    uint32_t              timeLimitSec = 3600;
    bool                  localMode = true;  // keys and screen on the caller stream (sysop at the console)
    std::FILE*            trace = nullptr;
    uint32_t              carrierGraceMs = 15000;  // after a hang-up the door may save and exit; then it is stopped
    ImageSource*          imageSource = nullptr;  // nullptr: relocate into RAM (the PC)
    std::vector<std::pair<uint8_t*, size_t>> arenas;  // host memory lent for pages first (counts in ramBudget)
    std::function<void()> onOutOfMemory;          // told the moment a page can't be had (diagnostics)
    DropFile              dropFile = DropFile::None;  // written into the door folder before start
    CallerInfo            caller;
    std::function<uint64_t()> clockMs;     // default: steady clock
    std::function<void(uint32_t)> sleepMs; // default: this thread sleeps
};

class DoorSession {
public:
    DoorSession(DoorConfig cfg, HostFs& fs, LockTable& locks, ByteStream& caller, int owner);
    ~DoorSession();
    DoorResult run();                 // until the door finishes
    bool       step(uint32_t maxMs);  // run up to maxMs; false once finished
    DoorResult result() const { return result_; }
    int        exitCode() const;
    GuestMemory::Stats memStats() const;
    size_t     poolPages() const { return pool_ ? pool_->usedPages() : 0; }
    size_t     poolBudget() const { return pool_ ? pool_->budgetPages() : 0; }
    std::array<std::string, 25> screen() const;
    std::vector<uint8_t> textPage() const { return bios_ ? bios_->textPage() : std::vector<uint8_t>(); }
    uint32_t   instructions() const { return instructions_; }
    std::string debugState() const;  // registers and the bytes at CS:IP, for tracing

    // services beyond DOS and BIOS (the FOSSIL driver joins here in remote mode)
    void addService(std::function<bool(int)> s) { extra_ = std::move(s); }

private:
    bool load();
    DoorConfig                  cfg_;
    HostFs&                     fs_;
    LockTable&                  locks_;
    ByteStream&                 caller_;
    int                         owner_;
    std::vector<uint8_t>        image_;
    std::unique_ptr<PagePool>   pool_;
    std::unique_ptr<GuestMemory> mem_;
    std::unique_ptr<Cpu>        cpu_;
    std::unique_ptr<Bios>       bios_;
    std::unique_ptr<Dos>        dos_;
    std::unique_ptr<Fossil>     fossil_;
    DosMemory*                  dm_ = nullptr;
    std::unique_ptr<ByteStream> console_;
    std::function<bool(int)>    extra_;
    DoorResult                  result_ = DoorResult::Running;
    bool                        loaded_ = false;
    uint64_t                    startMs_ = 0;
    uint32_t                    instructions_ = 0;
    uint32_t                    intsTraced_ = 0;
    bool                        carrierLost_ = false;
    uint64_t                    lostAtMs_ = 0;
};

}  // namespace door
}  // namespace axis
