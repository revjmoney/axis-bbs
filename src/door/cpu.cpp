// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "door/cpu.h"

#include <cassert>

#include "i8086.h"

namespace axis {
namespace door {

using fabgl::i8086;

namespace {
Cpu*            g_active = nullptr;  // the core is static: one Cpu at a time
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
    assert(g_active == nullptr && "only one Cpu may exist at a time");
    g_active = this;
}

Cpu::~Cpu() {
    g_active = nullptr;
    g_handler = nullptr;
    g_in = nullptr;
    g_out = nullptr;
}

void Cpu::reset(uint16_t cs, uint16_t ip, uint16_t ss, uint16_t sp, uint16_t ds, uint16_t es) {
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

void Cpu::setIntHandler(IntHandler h) { g_handler = std::move(h); }

void Cpu::setPorts(PortIn in, PortOut out) {
    g_in = std::move(in);
    g_out = std::move(out);
}

uint32_t Cpu::run(uint32_t maxInstructions) {
    g_stop = false;
    uint32_t n = 0;
    while (n < maxInstructions && !i8086::halted() && !g_stop) {
        i8086::step();
        n++;
    }
    return n;
}

void Cpu::requestStop() { g_stop = true; }
bool Cpu::halted() const { return i8086::halted(); }
void Cpu::raiseIrq(uint8_t vector) { i8086::IRQ(vector); }

uint16_t Cpu::ax() const { return i8086::reg(AX); }
uint16_t Cpu::bx() const { return i8086::reg(BX); }
uint16_t Cpu::cx() const { return i8086::reg(CX); }
uint16_t Cpu::dx() const { return i8086::reg(DX); }
uint16_t Cpu::si() const { return i8086::reg(SI); }
uint16_t Cpu::di() const { return i8086::reg(DI); }
uint16_t Cpu::bp() const { return i8086::reg(BP); }
uint16_t Cpu::sp() const { return i8086::reg(SP); }
uint16_t Cpu::cs() const { return i8086::reg(CS); }
uint16_t Cpu::ds() const { return i8086::reg(DS); }
uint16_t Cpu::es() const { return i8086::reg(ES); }
uint16_t Cpu::ss() const { return i8086::reg(SS); }
uint16_t Cpu::ip() const { return i8086::IP(); }

void Cpu::setAx(uint16_t v) { i8086::setReg(AX, v); }
void Cpu::setBx(uint16_t v) { i8086::setReg(BX, v); }
void Cpu::setCx(uint16_t v) { i8086::setReg(CX, v); }
void Cpu::setDx(uint16_t v) { i8086::setReg(DX, v); }
void Cpu::setSi(uint16_t v) { i8086::setReg(SI, v); }
void Cpu::setDi(uint16_t v) { i8086::setReg(DI, v); }
void Cpu::setBp(uint16_t v) { i8086::setReg(BP, v); }
void Cpu::setSp(uint16_t v) { i8086::setReg(SP, v); }
void Cpu::setDs(uint16_t v) { i8086::setReg(DS, v); }
void Cpu::setEs(uint16_t v) { i8086::setReg(ES, v); }
void Cpu::setIp(uint16_t v) { i8086::setIP(v); }

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

bool Cpu::cf() const { return i8086::flagCF(); }
void Cpu::setCf(bool v) { i8086::setFlagCF(v); }
bool Cpu::zf() const { return i8086::flagZF(); }
void Cpu::setZf(bool v) { i8086::setFlagZF(v); }

}  // namespace door
}  // namespace axis
