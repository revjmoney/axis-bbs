// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "door/cpu.h"

#include "i8086.h"

namespace axis {
namespace door {

using fabgl::i8086;

namespace {
Cpu*            g_active = nullptr;  // whose registers are in the static core right now
Cpu::IntHandler g_handler;
Cpu::PortIn     g_in;
Cpu::PortOut    g_out;
volatile bool   g_stop = false;

enum { AX = 0, CX, DX, BX, SP, BP, SI, DI, ES, CS, SS, DS };

bool    onInterrupt(void*, int num) { return g_handler ? g_handler(num) : false; }
uint8_t onReadPort(void*, int port) { return g_in ? g_in(port) : 0xFF; }
void    onWritePort(void*, int port, uint8_t v) {
    if (g_out) g_out(port, v);
}
}  // namespace

Cpu::Cpu(MemoryBus& mem) : mem_(mem) {
    if (g_active && g_active != this) g_active->suspend();
    g_active = this;
}

Cpu::~Cpu() {
    if (g_active == this) {
        g_active = nullptr;
        g_handler = nullptr;
        g_in = nullptr;
        g_out = nullptr;
    }
}

void Cpu::suspend() const {
    i8086::saveSnap(snap_);
    snapValid_ = true;
}

void Cpu::activate() const {
    if (g_active == this) return;
    if (g_active) g_active->suspend();
    g_active = const_cast<Cpu*>(this);
    if (snapValid_) i8086::loadSnap(snap_);
    g_handler = handler_;
    g_in = in_;
    g_out = out_;
    i8086::setBus(&mem_);
    i8086::setCallbacks(const_cast<Cpu*>(this), onReadPort, onWritePort, onInterrupt);
}

void Cpu::reset(uint16_t cs, uint16_t ip, uint16_t ss, uint16_t sp, uint16_t ds, uint16_t es) {
    activate();
    i8086::setBus(&mem_);
    i8086::setCallbacks(this, onReadPort, onWritePort, onInterrupt);
    i8086::reset();
    i8086::setReg(CS, cs);
    i8086::setIP(ip);
    i8086::setReg(SS, ss);
    i8086::setReg(SP, sp);
    i8086::setReg(DS, ds);
    i8086::setReg(ES, es);
    i8086::setFlagsWord(0x0202);  // interrupts on, as DOS starts a program
    g_stop = false;
}

void Cpu::setIntHandler(IntHandler h) {
    handler_ = std::move(h);
    if (g_active == this) g_handler = handler_;
}

void Cpu::setPorts(PortIn in, PortOut out) {
    in_ = std::move(in);
    out_ = std::move(out);
    if (g_active == this) {
        g_in = in_;
        g_out = out_;
    }
}

uint32_t Cpu::run(uint32_t maxInstructions) {
    activate();
    g_stop = false;
    uint32_t n = 0;
    while (n < maxInstructions && !g_stop) {
        // a halted CPU re-runs its HLT, which lets a pending interrupt (the timer) wake it;
        // still halted after that means nothing is due yet, so give the slice back
        bool wasHalted = i8086::halted();
        i8086::step();
        n++;
        if (wasHalted && i8086::halted()) break;
    }
    return n;
}

void Cpu::requestStop() { g_stop = true; }
bool Cpu::halted() const { activate(); return i8086::halted(); }
void Cpu::raiseIrq(uint8_t vector) { activate(); i8086::IRQ(vector); }

uint16_t Cpu::ax() const { activate(); return i8086::reg(AX); }
uint16_t Cpu::bx() const { activate(); return i8086::reg(BX); }
uint16_t Cpu::cx() const { activate(); return i8086::reg(CX); }
uint16_t Cpu::dx() const { activate(); return i8086::reg(DX); }
uint16_t Cpu::si() const { activate(); return i8086::reg(SI); }
uint16_t Cpu::di() const { activate(); return i8086::reg(DI); }
uint16_t Cpu::bp() const { activate(); return i8086::reg(BP); }
uint16_t Cpu::sp() const { activate(); return i8086::reg(SP); }
uint16_t Cpu::cs() const { activate(); return i8086::reg(CS); }
uint16_t Cpu::ds() const { activate(); return i8086::reg(DS); }
uint16_t Cpu::es() const { activate(); return i8086::reg(ES); }
uint16_t Cpu::ss() const { activate(); return i8086::reg(SS); }
uint16_t Cpu::ip() const { activate(); return i8086::IP(); }

void Cpu::setAx(uint16_t v) { activate(); i8086::setReg(AX, v); }
void Cpu::setBx(uint16_t v) { activate(); i8086::setReg(BX, v); }
void Cpu::setCx(uint16_t v) { activate(); i8086::setReg(CX, v); }
void Cpu::setDx(uint16_t v) { activate(); i8086::setReg(DX, v); }
void Cpu::setSi(uint16_t v) { activate(); i8086::setReg(SI, v); }
void Cpu::setDi(uint16_t v) { activate(); i8086::setReg(DI, v); }
void Cpu::setBp(uint16_t v) { activate(); i8086::setReg(BP, v); }
void Cpu::setSp(uint16_t v) { activate(); i8086::setReg(SP, v); }
void Cpu::setDs(uint16_t v) { activate(); i8086::setReg(DS, v); }
void Cpu::setEs(uint16_t v) { activate(); i8086::setReg(ES, v); }
void Cpu::setIp(uint16_t v) { activate(); i8086::setIP(v); }

uint8_t Cpu::al() const { return ax() & 0xFF; }
uint8_t Cpu::ah() const { return ax() >> 8; }
uint8_t Cpu::bl() const { return bx() & 0xFF; }
uint8_t Cpu::bh() const { return bx() >> 8; }
uint8_t Cpu::cl() const { return cx() & 0xFF; }
uint8_t Cpu::ch() const { return cx() >> 8; }
uint8_t Cpu::dl() const { return dx() & 0xFF; }
uint8_t Cpu::dh() const { return dx() >> 8; }
void    Cpu::setAl(uint8_t v) { setAx((ax() & 0xFF00) | v); }
void    Cpu::setAh(uint8_t v) { setAx((ax() & 0x00FF) | v << 8); }
void    Cpu::setBl(uint8_t v) { setBx((bx() & 0xFF00) | v); }
void    Cpu::setBh(uint8_t v) { setBx((bx() & 0x00FF) | v << 8); }
void    Cpu::setCl(uint8_t v) { setCx((cx() & 0xFF00) | v); }
void    Cpu::setCh(uint8_t v) { setCx((cx() & 0x00FF) | v << 8); }
void    Cpu::setDl(uint8_t v) { setDx((dx() & 0xFF00) | v); }
void    Cpu::setDh(uint8_t v) { setDx((dx() & 0x00FF) | v << 8); }

bool Cpu::cf() const { activate(); return i8086::flagCF(); }
void Cpu::setCf(bool v) { activate(); i8086::setFlagCF(v); }
bool Cpu::zf() const { activate(); return i8086::flagZF(); }
void Cpu::setZf(bool v) { activate(); i8086::setFlagZF(v); }

}  // namespace door
}  // namespace axis
