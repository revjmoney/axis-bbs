// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "door/session.h"

#include <cctype>
#include <chrono>
#include <thread>

#include "door/mz.h"

namespace axis {
namespace door {

namespace {
constexpr uint32_t kSlice = 20000;  // instructions between timer and housekeeping checks

// Local mode: keys from the caller stream, DOS text output drawn on the text screen.
class LocalConsole : public ByteStream {
public:
    LocalConsole(ByteStream& keys, Bios& bios) : keys_(keys), bios_(bios) {}
    int  available() override { return keys_.available(); }
    int  read() override { return keys_.read(); }
    void write(const uint8_t* p, size_t n) override {
        for (size_t i = 0; i < n; i++) bios_.ttyOut(p[i]);
    }
    bool carrier() override { return true; }

private:
    ByteStream& keys_;
    Bios&       bios_;
};
}  // namespace

DoorSession::DoorSession(DoorConfig cfg, HostFs& fs, LockTable& locks, ByteStream& caller, int owner)
    : cfg_(std::move(cfg)), fs_(fs), locks_(locks), caller_(caller), owner_(owner) {
    if (!cfg_.clockMs)
        cfg_.clockMs = [] {
            return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        };
    if (!cfg_.sleepMs) cfg_.sleepMs = [](uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
}

DoorSession::~DoorSession() {
    dos_.reset();  // closes the door's files and drops its locks
    bios_.reset();
    cpu_.reset();
    delete dm_;
    mem_.reset();
    pool_.reset();
}

bool DoorSession::load() {
    // read the program through the door's file system (SD, flash or PC alike)
    int h = fs_.open(cfg_.exe, 0, false, false);
    if (h < 0) return false;
    std::vector<uint8_t> file;
    uint8_t              buf[4096];
    for (int n; (n = fs_.read(h, buf, sizeof buf)) > 0;) file.insert(file.end(), buf, buf + n);
    fs_.close(h);
    MzInfo mz;
    if (!parseMz(file.data(), file.size(), mz)) return false;
    image_ = relocate(file.data(), file.size(), mz, kLoadSeg);
    file.clear();
    file.shrink_to_fit();

    pool_.reset(new PagePool(cfg_.ramBudget));
    mem_.reset(new GuestMemory(*pool_));
    cpu_.reset(new Cpu(*mem_));
    mem_->onOutOfMemory([this] { cpu_->requestStop(); });
    std::string exe = cfg_.exe;
    for (char& c : exe) c = (char)std::toupper((unsigned char)c);
    LoadedProgram lp;
    std::string   tail = cfg_.args.empty() ? "" : " " + cfg_.args;
    if (!loadProgram(*mem_, dm_, image_, mz, tail, "C:\\" + exe, lp)) return false;

    bios_.reset(new Bios(*cpu_, *mem_));
    bios_->install();
    dos_.reset(new Dos(*cpu_, *mem_, *dm_, fs_, locks_, owner_, lp.psp));
    dos_->setTrace(cfg_.trace);
    if (cfg_.localMode) {
        console_.reset(new LocalConsole(caller_, *bios_));
        dos_->setConsole(console_.get());
        bios_->setKeyboard(&caller_);
    } else {
        dos_->setConsole(&caller_);  // remote: plain DOS output goes to the caller; FOSSIL does the rest
    }
    bios_->setServices([this](int v) {
        if (v == 0x21) return dos_->int21();
        if (v == 0x20) return dos_->int20();
        return extra_ ? extra_(v) : false;
    });
    cpu_->setIntHandler([this](int n) {
        if (cfg_.trace && intsTraced_ < 5000 && n != Bios::kChainVector) {
            intsTraced_++;
            std::fprintf(cfg_.trace, "INT %02X AX=%04X BX=%04X CX=%04X DX=%04X from %04X:%04X\n", n, cpu_->ax(), cpu_->bx(),
                         cpu_->cx(), cpu_->dx(), cpu_->cs(), cpu_->ip());
        }
        return bios_->route(n);
    });
    cpu_->setPorts([this](int port) { return bios_->portIn(port); },
                   [this](int port, uint8_t v) { bios_->portOut(port, v); });
    cpu_->reset(lp.cs, lp.ip, lp.ss, lp.sp, lp.psp, lp.psp);
    return !mem_->outOfMemory();
}

bool DoorSession::step(uint32_t maxMs) {
    if (result_ != DoorResult::Running) return false;
    if (!loaded_) {
        loaded_ = true;
        if (!load()) {
            result_ = mem_ && mem_->outOfMemory() ? DoorResult::OutOfMemory : DoorResult::LoadError;
            return false;
        }
        startMs_ = cfg_.clockMs();
    }
    uint64_t sliceStart = cfg_.clockMs();
    while (true) {
        instructions_ += cpu_->run(kSlice);
        uint64_t now = cfg_.clockMs();
        bios_->tick(now);
        if (mem_->outOfMemory()) result_ = DoorResult::OutOfMemory;  // first: the door's data is now wrong
        else if (dos_->exited()) result_ = DoorResult::Exited;
        else if (!cfg_.localMode && !caller_.carrier()) result_ = DoorResult::CarrierLost;
        else if (now - startMs_ >= cfg_.timeLimitSec * 1000ull) result_ = DoorResult::TimeLimit;
        if (result_ != DoorResult::Running) return false;
        bool idle = bios_->consumeIdleHint() || dos_->waitingForKey() || bios_->waitingForKey();
        if (idle || cpu_->halted()) cfg_.sleepMs(2);  // the door is waiting: let the board breathe
        if (cfg_.clockMs() - sliceStart >= maxMs) return true;
    }
}

DoorResult DoorSession::run() {
    while (step(100)) {
    }
    return result_;
}

std::string DoorSession::debugState() const {
    if (!cpu_) return "not loaded";
    char b[200];
    uint32_t at = (cpu_->cs() * 16u + cpu_->ip()) & 0xFFFFF;
    std::snprintf(b, sizeof b, "CS:IP=%04X:%04X SS:SP=%04X:%04X DS=%04X ES=%04X AX=%04X BX=%04X CX=%04X DX=%04X bytes %02X %02X %02X %02X %02X %02X",
                  cpu_->cs(), cpu_->ip(), cpu_->ss(), cpu_->sp(), cpu_->ds(), cpu_->es(), cpu_->ax(), cpu_->bx(), cpu_->cx(), cpu_->dx(),
                  mem_->read8(at), mem_->read8(at + 1), mem_->read8(at + 2), mem_->read8(at + 3), mem_->read8(at + 4), mem_->read8(at + 5));
    return b;
}

int DoorSession::exitCode() const { return dos_ ? dos_->exitCode() : -1; }

GuestMemory::Stats DoorSession::memStats() const { return mem_ ? mem_->stats() : GuestMemory::Stats{0, 0, 0}; }

std::array<std::string, 25> DoorSession::screen() const {
    return bios_ ? bios_->screenText() : std::array<std::string, 25>{};
}

}  // namespace door
}  // namespace axis
