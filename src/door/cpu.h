// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The door's 8086: a thin wrapper over the vendored FabGL i8086 core (static, so one per process,
// which matches one door per board). Interrupts go to a C++ handler first (DOS, BIOS, FOSSIL).
#pragma once
#include <functional>

#include "door/membus.h"

namespace axis {
namespace door {

class Cpu {
public:
    using IntHandler = std::function<bool(int num)>;  // true = handled in C++, skip the IVT

    explicit Cpu(MemoryBus& mem);
    ~Cpu();
    void     reset(uint16_t cs, uint16_t ip, uint16_t ss, uint16_t sp, uint16_t ds, uint16_t es);
    void     setIntHandler(IntHandler h);
    uint32_t run(uint32_t maxInstructions);  // instructions executed; stops early on halt or requestStop()
    void     requestStop();
    bool     halted() const;
    void     raiseIrq(uint8_t vector);

    uint16_t ax() const; uint16_t bx() const; uint16_t cx() const; uint16_t dx() const;
    uint16_t si() const; uint16_t di() const; uint16_t bp() const; uint16_t sp() const;
    uint16_t cs() const; uint16_t ds() const; uint16_t es() const; uint16_t ss() const;
    uint16_t ip() const;
    void setAx(uint16_t v); void setBx(uint16_t v); void setCx(uint16_t v); void setDx(uint16_t v);
    void setSi(uint16_t v); void setDi(uint16_t v); void setBp(uint16_t v); void setSp(uint16_t v);
    void setDs(uint16_t v); void setEs(uint16_t v);
    uint8_t al() const; uint8_t ah() const; uint8_t bl() const; uint8_t bh() const;
    uint8_t cl() const; uint8_t ch() const; uint8_t dl() const; uint8_t dh() const;
    void setAl(uint8_t v); void setAh(uint8_t v); void setBl(uint8_t v); void setBh(uint8_t v);
    void setCl(uint8_t v); void setCh(uint8_t v); void setDl(uint8_t v); void setDh(uint8_t v);
    bool cf() const; void setCf(bool v);
    bool zf() const; void setZf(bool v);

private:
    MemoryBus& mem_;
};

}  // namespace door
}  // namespace axis
