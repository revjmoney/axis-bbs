// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "door/bios.h"

#include <ctime>

namespace axis {
namespace door {

namespace {
constexpr uint32_t kBda = 0x400, kText = 0xB8000;
constexpr int      kCols = 80, kRows = 25;
constexpr uint32_t kTicksPerDay = 0x1800B0;
uint8_t            bcd(int v) { return (uint8_t)((v / 10) << 4 | (v % 10)); }
// INT 14h gets an X00-style stub away from the others: a short jump over the FOSSIL
// signature 1954h at +6 and the highest function number at +8, which doors check
constexpr uint16_t kFossilStub = 0x800;
uint16_t           stubOffset(int v) { return v == 0x14 ? kFossilStub : (uint16_t)(v * 8); }
}  // namespace

Bios::Bios(Cpu& cpu, GuestMemory& mem) : cpu_(cpu), mem_(mem), rom_(kPageSize, 0) {}

void Bios::install() {
    // stubs: INT FBh, v, [INT 1Ch for the timer], IRET
    for (int v = 0; v < 256; v++) {
        uint8_t* s = &rom_[v * 8];
        int      n = 0;
        s[n++] = 0xCD;
        s[n++] = kChainVector;
        s[n++] = (uint8_t)v;
        if (v == 0x08) {
            s[n++] = 0xCD;
            s[n++] = 0x1C;
        }
        s[n++] = 0xCF;
    }
    const uint8_t fossil[] = {0xEB, 0x07, 0x90, 0x90, 0x90, 0x90, 0x54, 0x19, 0x1B, 0xCD, kChainVector, 0x14, 0xCF};
    for (size_t i = 0; i < sizeof fossil; i++) rom_[kFossilStub + i] = fossil[i];
    mem_.mapReadOnly(kStubSeg * 16u, rom_.data(), kPageSize);
    for (int v = 0; v < 256; v++) {
        mem_.write16(v * 4u, stubOffset(v));
        mem_.write16(v * 4u + 2, kStubSeg);
    }
    // BIOS data area
    mem_.write16(kBda + 0x00, 0x03F8);  // COM1
    mem_.write16(kBda + 0x10, 0x0220);  // equipment: 80x25 colour, one serial port
    mem_.write16(kBda + 0x13, 640);     // KB of conventional memory
    mem_.write8(kBda + 0x49, 3);        // video mode 3
    mem_.write16(kBda + 0x4A, kCols);
    mem_.write16(kBda + 0x4C, 4096);
    mem_.write16(kBda + 0x50, 0);       // cursor page 0
    mem_.write8(kBda + 0x62, 0);
    mem_.write16(kBda + 0x63, 0x03D4);
    mem_.write8(kBda + 0x84, kRows - 1);
    mem_.write16(kBda + 0x1A, 0x1E);    // empty keyboard buffer
    mem_.write16(kBda + 0x1C, 0x1E);
    mem_.mapRam(kText, kPageSize);
    for (int i = 0; i < kCols * kRows; i++) mem_.write16(kText + i * 2u, 0x0720);
}

bool Bios::isStub(int v) const {
    return const_cast<GuestMemory&>(mem_).read16(v * 4u + 2) == kStubSeg &&
           const_cast<GuestMemory&>(mem_).read16(v * 4u) == stubOffset(v);
}

bool Bios::service(int v) {
    if (dispatch(v)) return true;
    if (services_ && services_(v)) return true;
    if (v == 0x67) cpu_.setAh(0x80);  // EMS calls: no EMS here
    return true;                      // anything else: a no-op, as an IRET would be
}

bool Bios::route(int n) {
    if (n == kChainVector) {  // a stub: vector number follows the INT
        uint16_t ip = cpu_.ip();
        int      v = mem_.read8((cpu_.cs() * 16u + ip) & 0xFFFFF);
        cpu_.setIp((uint16_t)(ip + 1));
        service(v);
        // a handler that waits for a key rewinds 2 bytes (the length of a plain INT); from a
        // stub that lands one byte short of its start, so step back to the INT FBh itself
        if (cpu_.ip() == (uint16_t)(ip - 1)) cpu_.setIp((uint16_t)(ip - 2));
        return true;
    }
    if (n == 0x08) return false;  // hardware timer: always through the vector (stub or a door's hook)
    if (isStub(n)) return service(n);
    return false;  // the door hooked this vector: run its handler
}

uint8_t Bios::curRow() const { return const_cast<GuestMemory&>(mem_).read8(kBda + 0x51); }
uint8_t Bios::curCol() const { return const_cast<GuestMemory&>(mem_).read8(kBda + 0x50); }

void Bios::setCursor(int row, int col) {
    mem_.write8(kBda + 0x50, (uint8_t)col);
    mem_.write8(kBda + 0x51, (uint8_t)row);
}

void Bios::put(int row, int col, uint8_t ch, uint8_t attr) {
    if (row < 0 || row >= kRows || col < 0 || col >= kCols) return;
    mem_.write16(kText + (row * kCols + col) * 2u, (uint16_t)(ch | attr << 8));
}

void Bios::scroll(bool up, int lines, uint8_t attr, int r1, int c1, int r2, int c2) {
    if (r2 >= kRows) r2 = kRows - 1;
    if (c2 >= kCols) c2 = kCols - 1;
    int height = r2 - r1 + 1;
    if (lines == 0 || lines > height) lines = height;  // 0 = clear the window
    for (int i = 0; i < height; i++) {
        int dst = up ? r1 + i : r2 - i;
        int src = up ? dst + lines : dst - lines;
        for (int c = c1; c <= c2; c++) {
            uint16_t cell = (src >= r1 && src <= r2) ? mem_.read16(kText + (src * kCols + c) * 2u) : (uint16_t)(' ' | attr << 8);
            mem_.write16(kText + (dst * kCols + c) * 2u, cell);
        }
    }
}

void Bios::teletype(uint8_t ch) {
    int row = curRow(), col = curCol();
    switch (ch) {
        case '\r': col = 0; break;
        case '\n': row++; break;
        case 8:
            if (col) col--;
            break;
        case 7: break;  // bell
        default: {
            uint8_t attr = mem_.read8(kText + (row * kCols + col) * 2u + 1);
            put(row, col, ch, attr ? attr : 0x07);
            if (++col >= kCols) {
                col = 0;
                row++;
            }
        }
    }
    if (row >= kRows) {
        scroll(true, 1, 0x07, 0, 0, kRows - 1, kCols - 1);
        row = kRows - 1;
    }
    setCursor(row, col);
}

uint16_t Bios::keyCode(int c) const {
    switch (c) {
        case '\r': return 0x1C0D;
        case 27: return 0x011B;
        case 8:
        case 127: return 0x0E08;
        case '\t': return 0x0F09;
        case ' ': return 0x3920;
        default: return (uint16_t)(c & 0xFF);
    }
}

// A caller's terminal sends arrows and paging keys as ESC [ A, ESC [ 5 ~, ESC O A...: DOS programs want the
// PC's scan codes (a lone ESC is still Escape)
int Bios::nextKey(bool take) {
    if (pendingKey_ < 0 && kb_) {
        int c = kb_->read();
        if (c >= 0) {
            pendingKey_ = keyCode(c);
            int p = c == 27 ? kb_->peek() : -1;
            if (p == '[' || p == 'O') {
                kb_->read();
                int d = kb_->read();
                switch (d) {
                    case 'A': pendingKey_ = 0x4800; break;
                    case 'B': pendingKey_ = 0x5000; break;
                    case 'C': pendingKey_ = 0x4D00; break;
                    case 'D': pendingKey_ = 0x4B00; break;
                    case 'H': pendingKey_ = 0x4700; break;
                    case 'F': pendingKey_ = 0x4F00; break;
                    default:
                        if (d >= '1' && d <= '6') {
                            static const uint16_t tilde[] = {0x4700, 0x5200, 0x5300, 0x4F00, 0x4900, 0x5100};  // 1~ home .. 6~ page down
                            pendingKey_ = tilde[d - '1'];
                            if (kb_->peek() == '~') kb_->read();
                        }
                }
            }
        }
    }
    int k = pendingKey_;
    if (take) pendingKey_ = -1;
    return k;
}

bool Bios::keyboard() {
    uint8_t ah = cpu_.ah() & 0x0F;  // 10h-12h are the "enhanced" versions of 00h-02h
    if (ah == 0x00) {
        int c = nextKey(true);
        if (c < 0) {
            if (kb_) kb_->flush();  // the prompt has to be on the wire before this wait
            waiting_ = true;
            idle_ = true;
            cpu_.setIp((uint16_t)(cpu_.ip() - 2));
            cpu_.requestStop();  // nothing to do until a key arrives: give the slice back now
            return true;
        }
        cpu_.setAx((uint16_t)c);
        return true;
    }
    if (ah == 0x01) {
        int c = nextKey(false);
        if (c >= 0) {  // the waiting key, left in place for 00h to take
            cpu_.setZf(false);
            cpu_.setAx((uint16_t)c);
        } else {
            cpu_.setZf(true);
            idle_ = true;
        }
        return true;
    }
    if (ah == 0x02) {
        cpu_.setAl(0);
        return true;
    }
    return true;
}

bool Bios::dispatch(int v) {
    waiting_ = false;
    uint8_t ah = cpu_.ah(), al = cpu_.al();
    switch (v) {
        case 0x08: {  // timer tick (the stub then calls INT 1Ch)
            uint32_t t = mem_.read16(kBda + 0x6C) | (uint32_t)mem_.read16(kBda + 0x6E) << 16;
            if (++t >= kTicksPerDay) {
                t = 0;
                mem_.write8(kBda + 0x70, 1);
            }
            mem_.write16(kBda + 0x6C, (uint16_t)t);
            mem_.write16(kBda + 0x6E, (uint16_t)(t >> 16));
            return true;
        }
        case 0x10:
            switch (ah) {
                case 0x00:
                    mem_.write8(kBda + 0x49, al & 0x7F);
                    if (!(al & 0x80)) scroll(true, 0, 0x07, 0, 0, kRows - 1, kCols - 1);
                    setCursor(0, 0);
                    return true;
                case 0x01: return true;  // cursor shape
                case 0x02: setCursor(cpu_.dh(), cpu_.dl()); return true;
                case 0x03:
                    cpu_.setDx((uint16_t)(curRow() << 8 | curCol()));
                    cpu_.setCx(0x0607);
                    return true;
                case 0x05: return true;  // only page 0
                case 0x06:
                case 0x07:
                    scroll(ah == 0x06, al, cpu_.bh(), cpu_.ch(), cpu_.cl(), cpu_.dh(), cpu_.dl());
                    return true;
                case 0x08: {
                    uint16_t cell = mem_.read16(kText + (curRow() * kCols + curCol()) * 2u);
                    cpu_.setAx(cell);
                    return true;
                }
                case 0x09:
                case 0x0A: {
                    int row = curRow(), col = curCol();
                    for (int i = 0; i < cpu_.cx(); i++) {
                        int     c = col + i, r = row + c / kCols;
                        uint8_t attr = ah == 0x09 ? cpu_.bl() : mem_.read8(kText + (r * kCols + c % kCols) * 2u + 1);
                        put(r, c % kCols, al, attr);
                    }
                    return true;
                }
                case 0x0E: teletype(al); return true;
                case 0x0F:
                    cpu_.setAx((uint16_t)(kCols << 8 | mem_.read8(kBda + 0x49)));
                    cpu_.setBh(0);
                    return true;
                case 0x11:
                    if (al == 0x30) {  // font information: 16-point characters, 25 rows
                        cpu_.setCx(16);
                        cpu_.setDl(kRows - 1);
                        cpu_.setEs(kStubSeg);
                        cpu_.setBp(0xFA6E);
                    }
                    return true;
                case 0x12:
                    if (cpu_.bl() == 0x10) {  // EGA+: colour, 256 KB
                        cpu_.setBx(0x0003);
                        cpu_.setCx(0x0009);
                    }
                    return true;
                case 0x1A:
                    if (al == 0) {
                        cpu_.setAl(0x1A);
                        cpu_.setBx(0x0008);  // VGA colour
                    }
                    return true;
                default: return true;
            }
        case 0x11: cpu_.setAx(mem_.read16(kBda + 0x10)); return true;
        case 0x12: cpu_.setAx(640); return true;
        case 0x13:
            if (ah == 0x00) cpu_.setCf(false);
            else {
                cpu_.setAh(0x01);
                cpu_.setCf(true);
            }
            return true;
        case 0x15:
            if (cpu_.ax() == 0x1000 || ah == 0x86) {  // DESQview give up slice, BIOS wait
                idle_ = true;
                cpu_.setCf(false);
            } else if (ah == 0x88) {
                cpu_.setAx(0);  // no extended memory
                cpu_.setCf(false);
            } else {
                cpu_.setAh(0x86);
                cpu_.setCf(true);
            }
            return true;
        case 0x16: return keyboard();
        case 0x1A: {
            std::time_t t = std::time(nullptr);
            std::tm     lt = *std::localtime(&t);
            if (ah == 0x00) {
                cpu_.setCx(mem_.read16(kBda + 0x6E));
                cpu_.setDx(mem_.read16(kBda + 0x6C));
                cpu_.setAl(mem_.read8(kBda + 0x70));
                mem_.write8(kBda + 0x70, 0);
            } else if (ah == 0x02) {
                cpu_.setCh(bcd(lt.tm_hour));
                cpu_.setCl(bcd(lt.tm_min));
                cpu_.setDh(bcd(lt.tm_sec));
                cpu_.setCf(false);
            } else if (ah == 0x04) {
                cpu_.setCh(bcd((lt.tm_year + 1900) / 100));
                cpu_.setCl(bcd((lt.tm_year + 1900) % 100));
                cpu_.setDh(bcd(lt.tm_mon + 1));
                cpu_.setDl(bcd(lt.tm_mday));
                cpu_.setCf(false);
            }
            return true;
        }
        case 0x1B:  // Ctrl-Break
        case 0x1C:  // user timer hook: nothing by default
            return true;
        case 0x28: idle_ = true; return true;
        case 0x2F:
            cpu_.setAl(cpu_.ax() == 0x1000 ? 0xFF : 0x00);  // SHARE installed; nothing else (no XMS, no Windows)
            return true;
        case 0x33: cpu_.setAx(0); return true;  // no mouse
        default: return false;
    }
}

int Bios::tick(uint64_t nowMs) {
    if (!started_) {
        started_ = true;
        startMs_ = nowMs;
    }
    uint64_t due = (nowMs - startMs_) * 182065 / 10000000;
    if (due > raised_) {
        raised_++;
        cpu_.raiseIrq(0x08);
        return 1;
    }
    return 0;
}

uint8_t Bios::portIn(int port) {
    switch (port) {
        case 0x3DA:  // CGA/VGA status: retrace bits flip on every read so snow-avoidance loops finish
        case 0x3BA:
            retrace_ ^= 0x09;
            return retrace_;
        case 0x61: return 0x00;  // speaker / system control
        case 0x21:
        case 0xA1: return 0x00;  // interrupt masks: everything enabled
        default: return 0xFF;
    }
}

void Bios::portOut(int, uint8_t) {}  // PIC end-of-interrupt, speaker, CRT cursor: nothing to do

bool Bios::consumeIdleHint() {
    bool i = idle_;
    idle_ = false;
    return i;
}

std::vector<uint8_t> Bios::textPage() const {
    std::vector<uint8_t> v(kCols * kRows * 2);
    const_cast<GuestMemory&>(mem_).readBlock(kText, v.data(), v.size());
    return v;
}

std::array<std::string, 25> Bios::screenText() const {
    std::array<std::string, 25> s;
    for (int r = 0; r < kRows; r++) {
        s[r].resize(kCols);
        for (int c = 0; c < kCols; c++) s[r][c] = (char)const_cast<GuestMemory&>(mem_).read8(kText + (r * kCols + c) * 2u);
    }
    return s;
}

}  // namespace door
}  // namespace axis
