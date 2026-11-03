// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// High-level DOS: INT 21h answered in C++, no DOS kernel. Covers what doors use: files and
// directories, sharing and record locks, memory blocks, date and time, vectors, character I/O.
#pragma once
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

#include "door/bytestream.h"
#include "door/cpu.h"
#include "door/dosmem.h"
#include "door/guestmem.h"
#include "door/hostfs.h"

namespace axis {
namespace door {

class Dos {
public:
    Dos(Cpu& cpu, GuestMemory& mem, DosMemory& dm, HostFs& fs, LockTable& locks, int owner, uint16_t psp);
    ~Dos();
    bool int21();
    bool int20();
    bool exited() const { return exited_; }
    int  exitCode() const { return exitCode_; }
    void setTrace(std::FILE* f) { trace_ = f; }
    void setConsole(ByteStream* s) { con_ = s; }
    bool waitingForKey() const { return waiting_; }  // the last call rewound itself to wait for a key
    int  hostHandle(uint16_t dosHandle) const;       // -1 if not a file

private:
    uint32_t    lin(uint16_t seg, uint16_t off) const { return (seg * 16u + off) & 0xFFFFF; }
    std::string asciiz(uint16_t seg, uint16_t off) const;
    void        ok();
    void        fail(int code);
    int         newHandle(int host);
    bool        keyOrWait(int& c);
    void        tracef(const char* fmt, ...);

    Cpu&        cpu_;
    GuestMemory& mem_;
    DosMemory&  dm_;
    HostFs&     fs_;
    LockTable&  locks_;
    int         owner_;
    uint16_t    psp_;
    ByteStream* con_ = nullptr;
    std::FILE*  trace_ = nullptr;
    bool        exited_ = false, waiting_ = false;
    int         exitCode_ = 0, lastError_ = 0;
    uint16_t    dtaSeg_, dtaOff_ = 0x80;
    std::map<int, int>       handles_;  // DOS handle -> host handle (5 and up; 0-4 are the console)
    std::map<uint32_t, FindState> finds_;  // per DTA address
};

}  // namespace door
}  // namespace axis
