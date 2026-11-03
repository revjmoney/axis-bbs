# Door Runtime, PC Build (Stages 1 and 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A portable C++ door runtime (8086 core + paged guest memory + high-level DOS + BIOS + FOSSIL) that runs the real LORD 4.07 on a PC, first in local mode, then over telnet with a DOOR.SYS drop file, with the same source later compiled for the CYD.

**Architecture:** The FabGL i8086 core (MIT) is vendored and refactored so every memory access goes through a 4 KB page table (`GuestMemory`). DOS, BIOS and FOSSIL interrupts are handled in C++ through the core's interrupt callback (no DOS kernel). The runtime talks to the outside world only through two small interfaces, `HostFs` (files) and `ByteStream` (caller I/O), so the ESP32 port later swaps implementations, not logic. A PC command-line host (`doorhost`) runs a door locally or behind a telnet listener.

**Tech Stack:** C++17, CMake 4 + Ninja, MSYS2 mingw-w64 g++ 16 on Windows (also builds with g++ 13 on the i5 Ubuntu box), doctest 2.4.11 (MIT, single header) for tests, Winsock2 / BSD sockets for telnet.

**Spec:** `docs/superpowers/specs/2026-11-02-axis-underground-bbs-v1-design.md` (sections 3, 5.2, 5.8, 6 stages 1 and 2, 8). This is plan 1 of several; later plans cover the CYD port (stage 3), assembly optimization (4), BBS core (5), message bases (6), files (7), sysop (8), art (9), cluster (10) and native LORD (11).

## Global Constraints

- License: every new file starts with `// SPDX-License-Identifier: GPL-3.0-or-later` and `// Copyright (C) 2026 Rev. J. Money`. Vendored code keeps its own header and license file; our changes are noted at the top of the changed file.
- Reuse policy: proven open source first (MIT, GPL-3, public domain); refactor or rewrite in assembly only when measured better. emu2 is a behavior reference only; never copy its code.
- Portability: nothing under `src/door/` may include Windows, POSIX socket, ESP-IDF or Arduino headers. Platform code lives in `src/platform/pc/`.
- No door files, LORD data or keys in the repo. Tests that need LORD read its folder from the environment variable `AXIS_LORD_DIR` (on Rev's PC: `F:\axis-bbs-work\doors\lord407\game`) and are skipped when it is unset.
- Fixed guest layout: IVT 0x00000, BIOS data area 0x00400, DOS data 0x00500 to 0x00FFF, PSP segment **0x0100**, program load segment **0x0110**, conventional memory top segment **0xA000**, text video page **0xB8000** (4 KB, always RAM), BIOS stub page **0xF0000**.
- Door RAM budget default **128 KB** (`DoorConfig::ramBudget`), page size **4096**, 256 pages in the 1 MB space.
- LORD facts the tests pin: LORD.EXE load image **348368** bytes, min alloc **129344** bytes, **14988** relocations; local run `LORD.EXE 0 NOEMS /DREW`; opens files with AL=**0x44**; LORDCFG writes `lord.dat` in lowercase; idles with INT 15h AX=**0x1000** and INT 16h AH=**0x01**.
- FOSSIL: AH=04h returns AX=**0x1954**, BL=**0x1B**, BH=**5**. Port 0 = COM1. Check every function against FSC-0015 / X00REF before Task 7 (the research notes could not load the spec text; their table in `F:\research_notes\CYD DOS door hosting\door_requirements.md` is from memory).
- DOOR.SYS is 52 CRLF lines in the order listed in `F:\research_notes\CYD DOS door hosting\door_requirements.md` (section "Drop files", from the Synchronet DOOR.SYS reference). Line 1 is `COM1:` for a remote caller, `COM0:` for local. Sysop name and BBS name are passed exactly as configured (LORD registration depends on them).
- Prose in docs and comments: no em dashes or double hyphens.

## Review Focus

1. A caller hangs up in the middle of a door: the door must see carrier loss and exit, or be stopped by a watchdog, and the session must end without hanging. Test: Task 8 `carrier drop ends LORD`.
2. The door needs more pages than the budget: the session must end with `DoorResult::OutOfMemory`, never crash or corrupt files. Test: Task 2 `budget exhaustion sets outOfMemory`, Task 6 `tiny budget ends session cleanly`.
3. Telnet clients send IAC negotiation, CR NUL, CR LF and literal 0xFF bytes: the door must receive clean keystrokes and 0xFF must round-trip. Test: Task 8 `telnet filter` cases.
4. A door spins without ever calling the idle hooks: the runtime must still advance the timer, service the network and enforce the time limit. Test: Task 6 `busy loop still times out`.
5. DOS file names in any case (`LORD.DAT` vs `lord.dat`, `C:\LORD\X.DAT` style paths): lookups must be case-insensitive and drive letters stripped. Test: Task 4 `case-insensitive open`.

---

## File Structure

```
CMakeLists.txt                     PC build: lib axisdoor, exe doorhost, exe axis_tests
third_party/i8086/                 FabGL i8086 core (MIT), refactored memory access
  i8086.h, i8086.cpp, LICENSE.TXT, AXIS_CHANGES.md
third_party/doctest/doctest.h      doctest 2.4.11 (MIT)
src/door/
  guestmem.h/.cpp                  PagePool, GuestMemory (page table, CoW, budget, stats)
  cpu.h/.cpp                       Cpu wrapper over the vendored core
  mz.h/.cpp                        MZ header parse, relocation, program load + PSP/env
  dosmem.h/.cpp                    MCB chain: alloc/resize/free (INT 21h 48h/49h/4Ah)
  hostfs.h                         HostFs interface, FileInfo, FindState, LockTable
  dos.h/.cpp                       INT 21h high-level DOS
  bytestream.h                     ByteStream interface + ScriptStream (tests)
  bios.h/.cpp                      BDA, INT 10h/15h/16h/1Ah, timer IRQ0 + INT 1Ch, screenText()
  fossil.h/.cpp                    INT 14h FOSSIL over a ByteStream
  dropfile.h/.cpp                  CallerInfo, DOOR.SYS, DORINFO1.DEF
  session.h/.cpp                   DoorConfig, DoorResult, DoorSession run loop, trace
src/net/telnet.h/.cpp              TelnetFilter (portable protocol logic)
src/platform/pc/
  stdhostfs.h/.cpp                 HostFs over std::filesystem (case-insensitive)
  consolestream.h/.cpp             raw keyboard + stdout ByteStream (local mode)
  tcpstream.h/.cpp                 TcpListener + TcpStream (Winsock2 / BSD)
tools/doorhost/main.cpp            CLI: --dir --exe --args --local | --telnet PORT --drop doorsys|dorinfo --budget KB --trace FILE
tests/
  test_main.cpp                    doctest main
  asm.h                            tiny helper: byte vectors for hand-assembled test programs
  test_cpu.cpp, test_guestmem.cpp, test_mz.cpp, test_dos.cpp, test_bios.cpp,
  test_session.cpp, test_fossil.cpp, test_dropfile.cpp, test_telnet.cpp,
  test_lord_local.cpp, test_lord_telnet.cpp
```

Namespace for everything in `src/`: `axis::door` (`axis::net` for telnet, `axis::pc` for platform).

---

### Task 1: Project skeleton and vendored 8086 core

**Files:**
- Create: `CMakeLists.txt`, `third_party/i8086/{i8086.h,i8086.cpp,LICENSE.TXT,AXIS_CHANGES.md}`, `third_party/doctest/doctest.h`, `src/door/cpu.h`, `src/door/cpu.cpp`, `tests/test_main.cpp`, `tests/asm.h`, `tests/test_cpu.cpp`, `.gitignore`

**Interfaces:**
- Produces:
  ```cpp
  namespace axis::door {
  struct MemoryBus {                      // implemented by GuestMemory in Task 2
    virtual uint8_t  read8(uint32_t a) = 0;
    virtual uint16_t read16(uint32_t a) = 0;
    virtual void     write8(uint32_t a, uint8_t v) = 0;
    virtual void     write16(uint32_t a, uint16_t v) = 0;
    virtual ~MemoryBus() = default;
  };
  class Cpu {
  public:
    using IntHandler = std::function<bool(int num)>;   // true = handled in C++, skip IVT
    explicit Cpu(MemoryBus& mem);
    void reset(uint16_t cs, uint16_t ip, uint16_t ss, uint16_t sp, uint16_t ds, uint16_t es);
    void setIntHandler(IntHandler h);
    uint32_t run(uint32_t maxInstructions);  // returns instructions executed; stops early on halt/requestStop
    void requestStop(); bool halted() const;
    void raiseIrq(uint8_t vector);
    uint16_t ax() const; /* bx cx dx si di bp sp cs ds es ss ip */  void setAx(uint16_t); /* ... */
    uint8_t al() const; uint8_t ah() const; /* bl bh cl ch dl dh */ void setAl(uint8_t); /* ... */
    bool cf() const; void setCf(bool); bool zf() const; void setZf(bool);
  };
  }
  ```
- Consumes: nothing.

- [ ] **Step 1: Vendor the sources.** Copy `src/emudevs/i8086.h` and `i8086.cpp` from FabGL (github.com/fdivitto/FabGL, current master; a clone is at `~/axis/fabgl` on the i5) plus 8086tiny's MIT `LICENSE.TXT`, and doctest 2.4.11 `doctest.h`. Remove the `fabgl.h` include and FabGL-only helpers so the core builds standalone. Record every change in `AXIS_CHANGES.md`.
- [ ] **Step 2: Refactor memory access.** Replace the `MEM8`/`MEM16` macros and `RMEM8/16`, `WMEM8/16` (11 uses of `s_memory`) with calls through a static `axis::door::MemoryBus*` (`s_bus->read8(addr & 0xFFFFF)` etc.). Video-memory callbacks are removed (B800 is ordinary RAM in `GuestMemory`). Instruction fetch must go through the bus too (the core reads opcodes via `s_memory + ...`: replace with a small fetch buffer filled from `read8`).
- [ ] **Step 3: Write the failing test** `tests/test_cpu.cpp`, using a `FlatBus` (1 MB `std::vector<uint8_t>`) defined in the test:
  ```cpp
  TEST_CASE("cpu runs mov/add/hlt") {
    FlatBus bus; load(bus, 0x1000, {0xB8,0x34,0x12, 0x05,0x01,0x00, 0xF4}); // mov ax,1234h; add ax,1; hlt
    Cpu cpu(bus); cpu.reset(0x0100,0x0000, 0x0200,0xFFFE, 0x0100,0x0100);
    cpu.run(100);
    CHECK(cpu.ax() == 0x1235); CHECK(cpu.halted());
  }
  TEST_CASE("int 21h reaches the C++ handler") {
    FlatBus bus; load(bus, 0x1000, {0xB4,0x30, 0xCD,0x21, 0xF4}); // mov ah,30h; int 21h; hlt
    Cpu cpu(bus); cpu.reset(0x0100,0,0x0200,0xFFFE,0x0100,0x0100);
    int seen = -1; cpu.setIntHandler([&](int n){ seen = n; cpu.setAx(0x0005); return true; });
    cpu.run(100);
    CHECK(seen == 0x21); CHECK(cpu.ax() == 0x0005);
  }
  TEST_CASE("unhandled interrupt jumps through the IVT") { /* IVT[0x60] -> 0x2000:0000 holding mov bx,7; hlt; handler returns false */ }
  ```
- [ ] **Step 4: Run to see it fail.** `cmake -S . -B build -G Ninja && cmake --build build && build/axis_tests -tc="cpu*"`. Expected: build error (no `Cpu`).
- [ ] **Step 5: Implement `Cpu`** in `src/door/cpu.cpp` as a thin wrapper over the static core (one instance per process; assert if constructed twice). `CMakeLists.txt`: static lib `axisdoor` (src/door, src/net, third_party/i8086), exe `axis_tests` (tests + doctest), exe `doorhost`; flags `-O2 -Wall -Wextra`; on mingw link `ws2_32`.
- [ ] **Step 6: Run tests.** `build/axis_tests -tc="cpu*"`. Expected: 3 passed.
- [ ] **Step 7: Commit.** `git add -A && git commit -m "Door runtime: CMake skeleton, vendored FabGL i8086 core behind a MemoryBus"`

### Task 2: Paged guest memory with a RAM budget

**Files:**
- Create: `src/door/guestmem.h`, `src/door/guestmem.cpp`, `tests/test_guestmem.cpp`

**Interfaces:**
- Consumes: `MemoryBus` (Task 1).
- Produces:
  ```cpp
  constexpr uint32_t kPageSize = 4096; constexpr int kPageCount = 256;
  class PagePool {                         // individually allocated 4 KB blocks (no big buffer)
  public:
    explicit PagePool(size_t budgetBytes);
    uint8_t* alloc();                      // nullptr when the budget is used up
    void releaseAll();
    size_t usedPages() const; size_t peakPages() const; size_t budgetPages() const;
  };
  class GuestMemory : public MemoryBus {
  public:
    explicit GuestMemory(PagePool& pool);
    void mapReadOnly(uint32_t addr, const uint8_t* src, uint32_t len); // page-aligned; copy-on-write
    void mapZero(uint32_t addr, uint32_t len);                        // zero-on-first-write, reads 0
    void mapRam(uint32_t addr, uint32_t len);                         // allocate now (IVT, B800)
    void readBlock(uint32_t addr, void* dst, size_t n);
    void writeBlock(uint32_t addr, const void* src, size_t n);
    bool outOfMemory() const;              // a write needed a page and the pool was empty
    struct Stats { int ramPages; int roPages; int peakRamPages; };
    Stats stats() const;
    // MemoryBus overrides; unmapped reads return 0xFF, unmapped writes are dropped
  };
  ```

- [ ] **Step 1: Write failing tests:** `zero page reads zero and allocates on first write` (stats().ramPages goes 0 then 1); `read-only page copies on write` (source buffer unchanged, guest sees new byte, roPages decreases by 1, ramPages increases by 1); `word access across a page boundary` (write16 at 0x10FFF lands in two pages); `budget exhaustion sets outOfMemory` (pool of 2 pages, third written page leaves the byte unwritten and `outOfMemory()` true); `peak tracks the maximum`.
- [ ] **Step 2: Run, expect FAIL** (`build/axis_tests -tc="*page*"`).
- [ ] **Step 3: Implement.** Page entry = `{const uint8_t* ro; uint8_t* ram; uint8_t kind;}`; read path checks `ram` then `ro` then kind. Keep the read path branch-light; it becomes the assembly target in plan 4.
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit.** `git commit -am "Door runtime: 4 KB paged guest memory with copy-on-write and a RAM budget"`

### Task 3: MZ loader and DOS memory blocks

**Files:**
- Create: `src/door/mz.h/.cpp`, `src/door/dosmem.h/.cpp`, `tests/test_mz.cpp`

**Interfaces:**
- Consumes: `GuestMemory` (Task 2).
- Produces:
  ```cpp
  struct MzInfo { uint32_t imageSize, minAlloc, maxAlloc, headerSize; uint16_t relocCount, cs, ip, ss, sp; };
  bool parseMz(const uint8_t* file, size_t len, MzInfo& out);
  std::vector<uint8_t> relocate(const uint8_t* file, size_t len, const MzInfo& mz, uint16_t loadSeg);
  class DosMemory {                        // real MCB chain in guest memory, like DOS
  public:
    DosMemory(GuestMemory& mem, uint16_t firstSeg, uint16_t topSeg);
    bool alloc(uint16_t paras, uint16_t owner, uint16_t& seg, uint16_t& largest); // 48h
    bool free(uint16_t seg);                                                      // 49h
    bool resize(uint16_t seg, uint16_t paras, uint16_t& largest);                 // 4Ah
  };
  struct LoadedProgram { uint16_t psp, cs, ip, ss, sp; };
  LoadedProgram loadProgram(GuestMemory& mem, DosMemory& dm, const std::vector<uint8_t>& image,
                            const MzInfo& mz, const std::string& cmdTail, const std::string& progPath);
  constexpr uint16_t kPspSeg = 0x0100, kLoadSeg = 0x0110, kTopSeg = 0xA000;
  ```
  `loadProgram` maps the relocated image read-only (it is the flash image on the CYD), maps the BSS/heap/stack area zero-on-write, builds the PSP (INT 20h at 0, top-of-memory word at 2, command tail at 80h, environment segment at 2Ch with `PATH=C:\` and the program path after the double NUL) and sets the initial registers (DS=ES=PSP).

- [ ] **Step 1: Failing tests:** `parses a tiny hand-built MZ` (header bytes in `asm.h`); `applies relocations at the load segment` (a fixup word 0x0000 becomes 0x0110 + value); `PSP has command tail and env`; `mcb alloc resize free` (alloc 0x100 paras, resize down, free, largest grows back); `LORD header values` (only if `AXIS_LORD_DIR`: imageSize 348368, minAlloc 129344, relocCount 14988).
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** per the interfaces. Program gets the largest block (as DOS does), so the door then shrinks itself with 4Ah.
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit.** `git commit -am "Door runtime: MZ loader, relocation, PSP and MCB memory"`

### Task 4: Host files and high-level DOS (INT 21h)

**Files:**
- Create: `src/door/bytestream.h`, `src/door/hostfs.h`, `src/platform/pc/stdhostfs.h/.cpp`, `src/door/dos.h/.cpp`, `tests/test_dos.cpp`

**Interfaces:**
- Consumes: `Cpu`, `GuestMemory`, `DosMemory`.
- Produces (`bytestream.h`, used by Dos here and by BIOS, FOSSIL and the session later):
  ```cpp
  class ByteStream {
  public:
    virtual int  available() = 0;           // bytes ready to read
    virtual int  read() = 0;                // -1 if none
    virtual void write(const uint8_t* p, size_t n) = 0;
    virtual bool carrier() = 0;
    virtual ~ByteStream() = default;
  };
  class ScriptStream : public ByteStream {  // tests: queued input, captured output
  public:
    void push(const std::string& keys); std::string output() const; void hangUp(); void clearOutput();
  };
  ```
- Produces (`hostfs.h`, `dos.h`):
  ```cpp
  struct FileInfo { uint32_t size; uint16_t dosDate, dosTime; uint8_t attr; std::string name83; };
  struct FindState { std::vector<FileInfo> matches; size_t next = 0; };
  class HostFs {
  public:
    virtual int  open(const std::string& dosPath, int access, bool create, bool truncate) = 0; // >=0 handle, <0 = -DOS error
    virtual int  read(int h, void* buf, int n) = 0;
    virtual int  write(int h, const void* buf, int n) = 0;   // n==0 truncates at the current position
    virtual int64_t seek(int h, int64_t off, int whence) = 0;
    virtual int  close(int h) = 0;
    virtual bool stat(const std::string& dosPath, FileInfo& out) = 0;
    virtual int  remove(const std::string& dosPath) = 0;
    virtual int  rename(const std::string& from, const std::string& to) = 0;
    virtual bool find(const std::string& dosPattern, uint8_t attr, FindState& st) = 0;
    virtual int  fileKey(int h) = 0;                          // stable id per file, for locks
    virtual ~HostFs() = default;
  };
  class LockTable { public: bool lock(int key, uint32_t off, uint32_t len, int owner); bool unlock(int key, uint32_t off, uint32_t len, int owner); void releaseOwner(int owner); };
  class StdHostFs : public HostFs { public: explicit StdHostFs(std::filesystem::path root); };
  class Dos {
  public:
    Dos(Cpu& cpu, GuestMemory& mem, DosMemory& dm, HostFs& fs, LockTable& locks, int owner);
    bool int21();                 // dispatch on AH; returns true (always handled)
    bool int20();                 // terminate
    bool exited() const; int exitCode() const;
    void setTrace(std::FILE* f);  // one line per call: "21 AH=3D AL=44 'LORD.DAT' -> 5"
    void setConsole(ByteStream* s); // character I/O target (functions 01h to 0Ch); null = drop output, no keys
  };
  ```
  Functions in this task: 01 02 06 07 08 09 0A 0B 0C (character I/O through `setConsole`), 0E 19 1A 25 2A 2B 2C 2D 2F 30 33 35 36 39 3A 3B 3C 3D 3E 3F 40 41 42 43 44(00,01,06,07,08,0E) 45 46 47 48 49 4A 4C 4D 4E 4F 56 57 59 5A 5B 5C 62 68 (commit, no-op). Unknown functions: trace them, set CF and AX=1.
  Rules learned from LORD: access mode is `AL & 3` (AL=44h opens read-only); sharing bits are accepted and recorded; lookups are case-insensitive and strip the drive letter; created files keep the case the door asked for; 5Ch locks go through `LockTable` with the session's owner id; SHARE is reported installed (INT 2Fh AX=1000h returns AL=FFh, handled in Task 5's dispatcher).

- [ ] **Step 1: Failing tests** (call `dos.int21()` directly after setting registers and writing strings into guest memory): `open with AL=44h reads LORD-style`; `case-insensitive open` (file `lord.dat` on disk, open `C:\LORD.DAT`); `create write seek read close`; `find first next with wildcard`; `alloc and resize through 48h 4Ah`; `5Ch lock conflict between two owners` (second owner gets CF set, AX=21h); `exit 4Ch sets exitCode`; `unknown function sets CF and traces`.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** `StdHostFs` (case-insensitive by scanning the directory and caching a lowercase map) and `Dos`.
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit.** `git commit -am "Door runtime: high-level DOS INT 21h over HostFs, record locks"`

### Task 5: BIOS, timer and text screen

**Files:**
- Create: `src/door/bios.h/.cpp`, `tests/test_bios.cpp`
- Consumes: `ByteStream`, `ScriptStream` (Task 4), `Cpu`, `GuestMemory`.

**Interfaces:**
- Produces:
  ```cpp
  class Bios {
  public:
    Bios(Cpu& cpu, GuestMemory& mem);
    void install();                          // IVT entries -> F000 stubs, BDA (80x25, COM1 at 3F8, equipment word)
    void setKeyboard(ByteStream* kb);        // local keys (local mode) or null
    bool dispatch(int vector);               // 08,10,11,12,13,15,16,1A,1C,2F,28; false = let IVT run
    void tick(uint64_t nowMs);               // raises IRQ0 at 18.2065 Hz from wall time; BDA tick count at 0x46C
    bool consumeIdleHint();                  // true once after INT 16h AH=01 with no key, INT 28h, or INT 15h AX=1000h
    std::array<std::string, 25> screenText() const;   // B800 page as text (attribute bytes dropped)
  };
  ```
  INT 10h: set mode 3, get mode, set/get cursor, write char/attr (09h/0Ah/0Eh) into B800, scroll up/down (06h/07h). INT 16h: 00h/10h read (blocks by asking the session to yield until a key), 01h/11h peek, 02h shift flags 0. INT 1Ah 00h ticks, 02h/04h RTC from host time. INT 15h AX=1000h idle. INT 2Fh AX=1000h SHARE installed (AL=FFh), everything else AL=0.

- [ ] **Step 1: Failing tests:** `teletype writes into B800 and screenText shows it`; `scroll moves lines up`; `int16 01 with no key sets ZF and idle hint`; `int16 00 returns pushed key`; `tick raises irq0 about 18 times a second` (simulate 1000 ms, count IRQ0 raises 18 or 19); `int15 1000h sets idle hint`; `share is reported installed`.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.** Session code (Task 6) owns the yield; BIOS only reports hints and blocking needs.
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit.** `git commit -am "Door runtime: BIOS services, 18.2 Hz timer, B800 text screen"`

### Task 6: Door session and local LORD (Stage 1 exit)

**Files:**
- Create: `src/door/session.h/.cpp`, `src/platform/pc/consolestream.h/.cpp`, `tools/doorhost/main.cpp`, `tests/test_session.cpp`, `tests/test_lord_local.cpp`

**Interfaces:**
- Consumes: everything above.
- Produces:
  ```cpp
  enum class DoorResult { Exited, OutOfMemory, CarrierLost, TimeLimit, LoadError };
  struct DoorConfig {
    std::filesystem::path dir; std::string exe; std::string args;
    size_t ramBudget = 128 * 1024; uint32_t timeLimitSec = 3600;
    bool localMode = true; std::FILE* trace = nullptr;
  };
  class DoorSession {
  public:
    DoorSession(DoorConfig cfg, HostFs& fs, LockTable& locks, ByteStream& caller, int owner);
    DoorResult run();                       // until exit, OOM, carrier loss or time limit
    int exitCode() const; GuestMemory::Stats memStats() const;
    std::array<std::string, 25> screen() const;   // for tests and the sysop mirror
  };
  ```
  Run loop: slices of 20,000 instructions; after each slice call `bios.tick(now)`; when `consumeIdleHint()` is true, sleep 2 ms (PC) or yield (CYD); stop on `dos.exited()`, `mem.outOfMemory()`, `!caller.carrier()` (remote mode), or time limit. In local mode the caller stream is the keyboard and the screen is B800 (the console host renders it).

- [ ] **Step 1: Failing tests** in `test_session.cpp` with hand-assembled COM-style programs wrapped in a tiny MZ: `program exits with code 3` (mov ax,4C03h; int 21h); `busy loop still times out` (jmp $ with timeLimitSec=1 returns TimeLimit within 2 s); `tiny budget ends session cleanly` (budget 8 KB, program writes 64 KB, returns OutOfMemory).
- [ ] **Step 2: Failing LORD test** in `test_lord_local.cpp` (skipped without `AXIS_LORD_DIR`; a fixture copies the LORD folder to a temp dir; if it has no `lord.dat`, the fixture runs `LORDCFG.EXE` through `DoorSession` and answers `A` then `x`, which also exercises the runtime): run `LORD.EXE 0 NOEMS /DREW` in local mode with an expect helper that polls `screen()`:
  ```cpp
  expectScreen("Name or handle"); push("\r");
  expectScreen("more|Press a key"); push("\r");      // intro art
  expectScreen("choice, warrior"); push("E");
  expectScreen("join this place"); push("Y");
  expectScreen("Name:"); push("Revman\r");
  expectScreen("\\[ Y \\]"); push("\r");
  expectScreen("gender"); push("M");
  expectScreen("Pick one"); push("K");
  // read the news, reach town, go to the forest, fight once, return, quit
  expectScreen("Your command"); push("F");
  expectScreen("The Forest"); push("L");
  expectScreen("Attack"); push("A");               // repeat A until the fight ends
  expectScreen("Your command"); push("R");
  expectScreen("Your command"); push("Q");          // town: quit, confirm
  CHECK(result == DoorResult::Exited);
  MESSAGE("peak RAM pages: " << stats.peakRamPages); CHECK(stats.peakRamPages * 4 <= 128);
  ```
  Prompt texts come from the stage 0 session logs; adjust the regexes to what LORD actually prints.
- [ ] **Step 3: Run, expect FAIL.**
- [ ] **Step 4: Implement** `DoorSession`, `ConsoleStream` (Windows console raw mode / termios; renders B800 to the terminal with ANSI), and `doorhost --local`.
- [ ] **Step 5: Run tests, expect PASS;** then play it by hand: `build/doorhost --dir F:\axis-bbs-work\doors\lord407\game --exe LORD.EXE --args "0 NOEMS /DREW" --local`. Expected: LORD's intro, menus and a fight work at the keyboard.
- [ ] **Step 6: Update `docs/FEATURES.md`** (Real DOS doors: in progress; note the measured peak RAM) and the brain note `D:\brain\Projects\AXIS Underground CNC.md` status log.
- [ ] **Step 7: Commit.** `git commit -am "Door runtime: session loop and doorhost; LORD 4.07 plays locally (stage 1)"`

### Task 7: FOSSIL driver and drop files

**Files:**
- Create: `src/door/fossil.h/.cpp`, `src/door/dropfile.h/.cpp`, `tests/test_fossil.cpp`, `tests/test_dropfile.cpp`
- Modify: `src/door/session.cpp` (wire INT 14h; write the drop file into the door's node folder before start in remote mode)

**Interfaces:**
- Produces:
  ```cpp
  class Fossil {
  public:
    Fossil(Cpu& cpu, GuestMemory& mem, ByteStream& remote);
    bool int14();                            // AH 00h..1Bh, 1Ch same as 04h; others AX=FFFFh
  };
  struct CallerInfo {
    std::string realName, alias, location, password = "*"; int security = 10;
    int node = 1; int minutesLeft = 60; int comPort = 1; long baud = 38400;
    std::string sysopName = "Rev J Money", bbsName = "Axis Underground CNC";
    bool ansi = true; int userRecord = 1; int timesOn = 1;
  };
  std::string makeDoorSys(const CallerInfo& c);     // 52 CRLF lines
  std::string makeDorinfo(const CallerInfo& c);     // DORINFO1.DEF, 13 lines
  ```
- [ ] **Step 1: Verify the FOSSIL function list** against FSC-0015 / X00REF (find a copy; the research notes' table is from memory) and note any differences in `src/door/fossil.cpp`'s header comment.
- [ ] **Step 2: Failing tests:** `init returns 1954h, BL=1Bh, BH=5`; `transmit with wait writes to the stream`; `status shows data ready, THRE and carrier bits`; `status drops carrier after hangUp`; `receive with wait returns pushed byte`; `block read and write (18h, 19h)`; `driver info structure (1Bh)` (fields at the offsets listed in the research notes); `handler signature word 1954h at vector offset +6`. DOOR.SYS: `52 lines with CRLF`, `line 1 COM1: remote and COM0: local`, `sysop and BBS names exact`, `line 40 record locking Y`.
- [ ] **Step 3: Run, expect FAIL.**
- [ ] **Step 4: Implement.**
- [ ] **Step 5: Run, expect PASS.**
- [ ] **Step 6: Commit.** `git commit -am "Door runtime: FOSSIL INT 14h and DOOR.SYS / DORINFO1.DEF"`

### Task 8: Telnet bridge and remote LORD (Stage 2 exit)

**Files:**
- Create: `src/net/telnet.h/.cpp`, `src/platform/pc/tcpstream.h/.cpp`, `tests/test_telnet.cpp`, `tests/test_lord_telnet.cpp`
- Modify: `tools/doorhost/main.cpp` (`--telnet PORT --drop doorsys|dorinfo`)

**Interfaces:**
- Produces:
  ```cpp
  namespace axis::net {
  class TelnetFilter {
  public:
    std::string initialNegotiation() const;  // IAC WILL ECHO, IAC WILL SGA, IAC DO SGA, IAC WILL BINARY, IAC DO BINARY, IAC DO NAWS, IAC DO TTYPE
    void fromNet(const uint8_t* p, size_t n, std::string& toApp, std::string& replyToNet); // strips IAC, answers options, CR NUL / CR LF -> CR, IAC IAC -> FF
    std::string toNet(const uint8_t* p, size_t n) const;   // FF -> IAC IAC
    int width() const; int height() const;   // from NAWS, default 80x25
  };
  }
  class TcpListener { public: explicit TcpListener(uint16_t port); std::unique_ptr<ByteStream> accept(int timeoutMs); };
  // TcpStream : ByteStream wraps a socket + TelnetFilter; carrier() false after the peer closes
  ```
- [ ] **Step 1: Failing telnet filter tests:** `strips IAC DO ECHO and replies WILL/WONT correctly`; `CR NUL becomes CR`; `CR LF becomes CR`; `IAC IAC becomes 0xFF`; `0xFF output is doubled`; `NAWS sets width and height`; `subnegotiation split across two reads`.
- [ ] **Step 2: Failing LORD remote test** (skipped without `AXIS_LORD_DIR`): start `DoorSession` in remote mode on a background thread with a `TcpStream` from `TcpListener(0)` (ephemeral port), write DOOR.SYS for node 1 (`CallerInfo{alias="Revman"}`), set LORD's node 1 to "Regular FOSSIL" and DOOR.SYS by copying a prepared `NODE1.DAT` made with LORDCFG (or drive LORDCFG in the test), connect a test client socket, and run the same expect script as Task 6 against the received ANSI stream (strip ANSI for matching). Then: `carrier drop ends LORD` (client closes mid-game; `run()` returns `CarrierLost` or `Exited` within 10 s and LORD's player file is not corrupted: reopen and find "Revman").
- [ ] **Step 3: Run, expect FAIL.**
- [ ] **Step 4: Implement** `TelnetFilter`, `TcpListener`/`TcpStream` (Winsock2 on Windows, BSD sockets elsewhere), and `doorhost --telnet`.
- [ ] **Step 5: Run tests, expect PASS;** then by hand: `build/doorhost --dir <lord> --exe LORD.EXE --args "1 /DREW" --telnet 2323 --drop doorsys` and connect with SyncTERM to `localhost:2323`. Expected: LORD plays over telnet in ANSI; quitting closes the session cleanly.
- [ ] **Step 6: Update `docs/FEATURES.md`** (FOSSIL/telnet working on the PC; drop files working) and the brain status log.
- [ ] **Step 7: Commit.** `git commit -am "Door runtime: telnet bridge; LORD 4.07 plays over telnet with DOOR.SYS (stage 2)"`
