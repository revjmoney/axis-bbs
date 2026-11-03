// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "door/fossil.h"

namespace axis {
namespace door {

namespace {
constexpr uint16_t kBufSize = 1024;
constexpr uint32_t kIdString = 0x0510;  // "AXIS FOSSIL" lives in the DOS data area (low RAM)
const char         kId[] = "AXIS FOSSIL for the CYD door runtime";
}  // namespace

Fossil::Fossil(Cpu& cpu, GuestMemory& mem, ByteStream& line) : cpu_(cpu), mem_(mem), line_(line) {}

void Fossil::fill() {
    while (in_.size() < kBufSize) {
        int c = line_.read();
        if (c < 0) break;
        in_.push_back((uint8_t)c);
    }
}

// AH = line status (01 data ready, 20 room for output, 40 output empty), AL = modem status
// (80 carrier detect; bit 3 is always set, as X00 and BNU do)
uint16_t Fossil::status() {
    fill();
    uint8_t ah = 0x60 | (in_.empty() ? 0 : 0x01);
    uint8_t al = 0x08 | (line_.carrier() ? 0x80 : 0);
    return (uint16_t)(ah << 8 | al);
}

bool Fossil::int14() {
    waiting_ = false;
    uint8_t ah = cpu_.ah(), al = cpu_.al();
    switch (ah) {
        case 0x00:  // set baud rate: nothing to set on a network connection
        case 0x03:
            cpu_.setAx(status());
            return true;
        case 0x01:  // transmit with wait
            line_.write(&al, 1);
            sent_ = true;
            cpu_.setAx(status());
            return true;
        case 0x02:  // receive with wait
            fill();
            if (in_.empty()) {
                if (!line_.carrier()) {  // no caller: report a timeout instead of waiting forever
                    cpu_.setAx(0x8000);
                    return true;
                }
                waiting_ = true;
                cpu_.setIp((uint16_t)(cpu_.ip() - 2));
                cpu_.requestStop();  // give the slice back until the caller types
                return true;
            }
            cpu_.setAx(in_.front());
            in_.pop_front();
            return true;
        case 0x04:  // initialise
        case 0x1C:  // X00 extended initialise
            cpu_.setAx(kSignature);
            cpu_.setBl(kHighestFunction);
            cpu_.setBh(kRevision);
            return true;
        case 0x05:  // de-initialise
        case 0x06:  // DTR
        case 0x08:  // flush output (we never buffer)
        case 0x09:  // purge output
        case 0x0F:  // flow control
        case 0x14:  // watchdog
        case 0x17:  // reboot: no
        case 0x1A:  // break
            return true;
        case 0x07:  // timer tick: INT 1Ch, 18.2 per second, 55 ms each
            cpu_.setAl(0x1C);
            cpu_.setAh(18);
            cpu_.setDx(55);
            return true;
        case 0x0A:  // purge input
            fill();
            in_.clear();
            return true;
        case 0x0B:  // transmit without wait
            line_.write(&al, 1);
            sent_ = true;
            cpu_.setAx(1);
            return true;
        case 0x0C:  // peek
            fill();
            cpu_.setAx(in_.empty() ? 0xFFFF : in_.front());
            return true;
        case 0x0D:  // local keyboard, no wait: the sysop has no keyboard in a remote session
            cpu_.setAx(0xFFFF);
            return true;
        case 0x0E:  // local keyboard, with wait: never comes, so wait politely
            waiting_ = true;
            cpu_.setIp((uint16_t)(cpu_.ip() - 2));
            cpu_.requestStop();
            return true;
        case 0x10:  // ^C / ^K check
            cpu_.setAx(0);
            return true;
        case 0x11:  // set cursor (local screen)
            mem_.write8(0x450, cpu_.dl());
            mem_.write8(0x451, cpu_.dh());
            return true;
        case 0x12:  // get cursor
            cpu_.setDl(mem_.read8(0x450));
            cpu_.setDh(mem_.read8(0x451));
            return true;
        case 0x13:  // write to the local screen with ANSI: the caller sees the door, not this
        case 0x15:
            return true;
        case 0x16:  // timer chain insert/delete: accepted
            cpu_.setAx(0);
            return true;
        case 0x18: {  // read block, no wait
            fill();
            uint32_t buf = (cpu_.es() * 16u + cpu_.di()) & 0xFFFFF;
            uint16_t n = 0;
            while (n < cpu_.cx() && !in_.empty()) {
                mem_.write8((buf + n++) & 0xFFFFF, in_.front());
                in_.pop_front();
            }
            cpu_.setAx(n);
            return true;
        }
        case 0x19: {  // write block, no wait
            uint32_t buf = (cpu_.es() * 16u + cpu_.di()) & 0xFFFFF;
            uint8_t  chunk[256];
            uint16_t done = 0, cx = cpu_.cx();
            while (done < cx) {
                uint16_t n = cx - done < sizeof chunk ? cx - done : (uint16_t)sizeof chunk;
                mem_.readBlock((buf + done) & 0xFFFFF, chunk, n);
                line_.write(chunk, n);
                sent_ = true;
                done += n;
            }
            cpu_.setAx(done);
            return true;
        }
        case 0x1B: {  // driver information
            mem_.writeBlock(kIdString, kId, sizeof kId);
            fill();
            uint8_t info[19] = {};
            auto    w16 = [&](int at, uint16_t v) {
                info[at] = (uint8_t)v;
                info[at + 1] = v >> 8;
            };
            w16(0x00, sizeof info);
            info[0x02] = kRevision;
            info[0x03] = 1;  // driver revision
            w16(0x04, kIdString & 0xF);
            w16(0x06, kIdString >> 4);
            w16(0x08, kBufSize);
            w16(0x0A, (uint16_t)(kBufSize - in_.size()));
            w16(0x0C, kBufSize);
            w16(0x0E, kBufSize);
            info[0x10] = 80;
            info[0x11] = 25;
            info[0x12] = 0x23;  // 38400 baud, 8 data bits, no parity, 1 stop bit
            uint16_t n = cpu_.cx() < sizeof info ? cpu_.cx() : (uint16_t)sizeof info;
            mem_.writeBlock((cpu_.es() * 16u + cpu_.di()) & 0xFFFFF, info, n);
            cpu_.setAx(n);
            return true;
        }
        default:
            cpu_.setAx(0xFFFF);  // not supported
            return true;
    }
}

}  // namespace door
}  // namespace axis
