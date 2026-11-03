// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "door/dos.h"

#include <algorithm>
#include <cstdarg>
#include <ctime>

namespace axis {
namespace door {

namespace {
enum : int {
    kNoFunction = 1, kNotFound = 2, kNoPath = 3, kTooMany = 4, kDenied = 5, kBadHandle = 6,
    kNoMemory = 8, kBadBlock = 9, kNoMoreFiles = 0x12, kLockViolation = 0x21
};
constexpr int kMaxHandles = 40;
}  // namespace

// LockTable lives here: it is part of the DOS sharing model
bool LockTable::lock(int key, uint32_t off, uint32_t len, int owner) {
    (void)owner;
    for (auto& l : locks_)
        if (l.key == key && off < l.off + l.len && l.off < off + len) return false;
    locks_.push_back({key, off, len, owner});
    return true;
}

bool LockTable::unlock(int key, uint32_t off, uint32_t len, int owner) {
    for (size_t i = 0; i < locks_.size(); i++)
        if (locks_[i].key == key && locks_[i].off == off && locks_[i].len == len && locks_[i].owner == owner) {
            locks_.erase(locks_.begin() + i);
            return true;
        }
    return false;
}

void LockTable::releaseOwner(int owner) {
    for (size_t i = locks_.size(); i-- > 0;)
        if (locks_[i].owner == owner) locks_.erase(locks_.begin() + i);
}

Dos::Dos(Cpu& cpu, GuestMemory& mem, DosMemory& dm, HostFs& fs, LockTable& locks, int owner, uint16_t psp)
    : cpu_(cpu), mem_(mem), dm_(dm), fs_(fs), locks_(locks), owner_(owner), psp_(psp), dtaSeg_(psp) {}

Dos::~Dos() {
    for (auto& r : refs_) fs_.close(r.first);
    locks_.releaseOwner(owner_);
}

int Dos::hostHandle(uint16_t dosHandle) const {
    auto it = handles_.find(dosHandle);
    return it == handles_.end() ? -1 : it->second;
}

std::string Dos::asciiz(uint16_t seg, uint16_t off) const {
    std::string s;
    for (uint32_t i = 0; i < 128; i++) {
        uint8_t c = mem_.read8(lin(seg, (uint16_t)(off + i)));
        if (!c) break;
        s += (char)c;
    }
    return s;
}

void Dos::ok() { cpu_.setCf(false); }

void Dos::fail(int code) {
    lastError_ = code;
    cpu_.setAx((uint16_t)code);
    cpu_.setCf(true);
}

int Dos::newHandle(int host, bool fresh) {
    for (int h = 5; h < kMaxHandles; h++)
        if (!handles_.count(h)) {
            handles_[h] = host;
            refs_[host]++;
            return h;
        }
    if (fresh) fs_.close(host);
    return -1;
}

void Dos::release(int dosHandle) {
    auto it = handles_.find(dosHandle);
    if (it == handles_.end()) return;
    int host = it->second;
    handles_.erase(it);
    if (--refs_[host] <= 0) {
        refs_.erase(host);
        fs_.close(host);
    }
}

void Dos::tracef(const char* fmt, ...) {
    if (!trace_) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(trace_, fmt, ap);
    va_end(ap);
    std::fflush(trace_);
}

// A key from the console, or rewind the INT 21h so the door asks again after the session yields.
bool Dos::keyOrWait(int& c) {
    c = con_ ? con_->read() : -1;
    if (c >= 0) return true;
    waiting_ = true;
    cpu_.setIp((uint16_t)(cpu_.ip() - 2));
    cpu_.requestStop();  // nothing to do until a key arrives: give the slice back now
    return false;
}

bool Dos::int20() {
    exited_ = true;
    exitCode_ = 0;
    cpu_.requestStop();  // nothing after the exit may run
    return true;
}

bool Dos::int21() {
    waiting_ = false;
    uint8_t  ah = cpu_.ah(), al = cpu_.al();
    uint16_t bx = cpu_.bx(), cx = cpu_.cx(), dx = cpu_.dx(), ds = cpu_.ds();
    auto     out = [&](uint8_t ch) {
        if (con_) con_->write(&ch, 1);
    };
    switch (ah) {
        case 0x00:  // terminate
            return int20();
        case 0x31:  // terminate and stay resident: nothing stays; the door is over
            exited_ = true;
            exitCode_ = al;
            cpu_.requestStop();
            return true;
        case 0x01:  // read key, echo
        case 0x07:  // read key, no echo
        case 0x08: {
            int c;
            if (!keyOrWait(c)) return true;
            cpu_.setAl((uint8_t)c);
            if (ah == 0x01) out((uint8_t)c);
            return true;
        }
        case 0x02:
            out(cpu_.dl());
            return true;
        case 0x06:  // direct console I/O
            if (cpu_.dl() == 0xFF) {
                int c = con_ ? con_->read() : -1;
                cpu_.setZf(c < 0);
                cpu_.setAl(c < 0 ? 0 : (uint8_t)c);
            } else {
                out(cpu_.dl());
            }
            return true;
        case 0x09:
            for (uint16_t i = 0; i < 0xFFFF; i++) {
                uint8_t c = mem_.read8(lin(ds, (uint16_t)(dx + i)));
                if (c == '$') break;
                out(c);
            }
            return true;
        case 0x0A: {  // buffered line input: wait until a whole line is there
            uint32_t buf = lin(ds, dx);
            uint8_t  max = mem_.read8(buf);
            if (!con_ || con_->available() == 0) {
                int c;
                keyOrWait(c);
                return true;
            }
            uint8_t n = 0;
            int     c;
            while ((c = con_->read()) >= 0 && c != '\r') {
                if ((c == 8 || c == 127) && n) n--;
                else if (n + 1 < max) mem_.write8(buf + 2 + n++, (uint8_t)c);
            }
            mem_.write8(buf + 1, n);
            mem_.write8(buf + 2 + n, '\r');
            return true;
        }
        case 0x0B:
            cpu_.setAl(con_ && con_->available() ? 0xFF : 0x00);
            return true;
        case 0x0C:  // flush input, then do function AL
            while (con_ && con_->read() >= 0) {
            }
            if (al == 0x01 || al == 0x06 || al == 0x07 || al == 0x08 || al == 0x0A) {
                cpu_.setAh(al);
                return int21();
            }
            return true;
        case 0x0D:  // disk reset
            return true;
        case 0x0E:  // select drive: only C:
            cpu_.setAl(26);
            return true;
        case 0x19:
            cpu_.setAl(2);  // C:
            return true;
        case 0x1A:
            dtaSeg_ = ds;
            dtaOff_ = dx;
            return true;
        case 0x25:
            mem_.write16(al * 4u, dx);
            mem_.write16(al * 4u + 2, ds);
            return true;
        case 0x2A:
        case 0x2C: {
            std::time_t t = std::time(nullptr);
            std::tm     lt = *std::localtime(&t);
            if (ah == 0x2A) {
                cpu_.setCx((uint16_t)(lt.tm_year + 1900));
                cpu_.setDh((uint8_t)(lt.tm_mon + 1));
                cpu_.setDl((uint8_t)lt.tm_mday);
                cpu_.setAl((uint8_t)lt.tm_wday);
            } else {
                cpu_.setCh((uint8_t)lt.tm_hour);
                cpu_.setCl((uint8_t)lt.tm_min);
                cpu_.setDh((uint8_t)lt.tm_sec);
                cpu_.setDl(0);
            }
            return true;
        }
        case 0x2B:
        case 0x2D:
            cpu_.setAl(0);  // setting the clock: accepted and ignored
            return true;
        case 0x2F:
            cpu_.setEs(dtaSeg_);
            cpu_.setBx(dtaOff_);
            return true;
        case 0x30:
            cpu_.setAx(0x0005);  // DOS 5.0
            cpu_.setBx(0);
            cpu_.setCx(0);
            return true;
        case 0x33:
            if (al == 0x00) cpu_.setDl(0);
            else if (al == 0x05) cpu_.setDl(3);  // boot drive C:
            return true;
        case 0x35:
            cpu_.setBx(mem_.read16(al * 4u));
            cpu_.setEs(mem_.read16(al * 4u + 2));
            return true;
        case 0x36:  // free space: plenty
            cpu_.setAx(8);
            cpu_.setBx(0x7FFF);
            cpu_.setCx(512);
            cpu_.setDx(0xFFFF);
            return true;
        case 0x39:
        case 0x3A:
        case 0x3B: {
            std::string p = asciiz(ds, dx);
            int         r = 0;
            if (ah == 0x39) r = fs_.mkdir(p);
            else if (ah == 0x3A) r = fs_.rmdir(p);
            else {
                std::string d = p.size() >= 2 && p[1] == ':' ? p.substr(2) : p;
                if (!fs_.isDir(d)) r = -kNoPath;
                else {
                    while (!d.empty() && d[0] == '\\') d.erase(0, 1);
                    fs_.setCwd(d.empty() || p[p.size() >= 3 && p[1] == ':' ? 2 : 0] == '\\' ? d
                                                                                          : (fs_.cwd().empty() ? d : fs_.cwd() + "\\" + d));
                }
            }
            if (r < 0) fail(-r);
            else ok();
            tracef("21 AH=%02X '%s' -> %d\n", ah, p.c_str(), r);
            return true;
        }
        case 0x3C:  // create or truncate
        case 0x3D:  // open
        case 0x5A:  // create temporary
        case 0x5B: {  // create new
            std::string p = asciiz(ds, dx);
            if (ah == 0x5A) {
                char name[16];
                std::snprintf(name, sizeof name, "AX%06u.TMP", (unsigned)(std::time(nullptr) % 1000000));
                p += name;
                mem_.writeBlock(lin(ds, (uint16_t)(dx + p.size() - std::string(name).size())), name, std::string(name).size() + 1);
            }
            int access = ah == 0x3D ? (al & 3) : 2;
            if (access == 3) access = 0;  // DOS ignores the reserved value; LORD opens with AL=44h
            int host = fs_.open(p, access, ah != 0x3D, ah == 0x3C, ah == 0x5B);
            int h = host < 0 ? host : newHandle(host);
            if (host >= 0 && h < 0) {
                fail(kTooMany);
            } else if (h < 0) {
                fail(-h == 80 ? 80 : -h);
            } else {
                cpu_.setAx((uint16_t)h);
                ok();
            }
            tracef("21 AH=%02X AL=%02X '%s' -> %d\n", ah, al, p.c_str(), h);
            return true;
        }
        case 0x3E: {
            if (handles_.count(bx)) {
                release(bx);
                ok();
            } else if (bx < 5) {
                ok();
            } else {
                fail(kBadHandle);
            }
            return true;
        }
        case 0x3F:
        case 0x40: {
            uint32_t buf = lin(ds, dx);
            if (bx < 5) {  // console handles
                if (ah == 0x40) {
                    for (uint16_t i = 0; i < cx; i++) out(mem_.read8((buf + i) & 0xFFFFF));
                    cpu_.setAx(cx);
                } else {
                    uint16_t n = 0;
                    int      c;
                    while (n < cx && con_ && (c = con_->read()) >= 0) mem_.write8((buf + n++) & 0xFFFFF, (uint8_t)c);
                    cpu_.setAx(n);
                }
                ok();
                return true;
            }
            int host = hostHandle(bx);
            if (host < 0) {
                fail(kBadHandle);
                return true;
            }
            uint8_t chunk[512];
            uint32_t done = 0;
            if (ah == 0x3F) {
                while (done < cx) {
                    int want = (int)std::min<uint32_t>(sizeof chunk, cx - done);
                    int got = fs_.read(host, chunk, want);
                    if (got <= 0) break;
                    mem_.writeBlock((buf + done) & 0xFFFFF, chunk, got);
                    done += got;
                    if (got < want) break;
                }
            } else if (cx == 0) {
                fs_.write(host, chunk, 0);
            } else {
                while (done < cx) {
                    int n = (int)std::min<uint32_t>(sizeof chunk, cx - done);
                    mem_.readBlock((buf + done) & 0xFFFFF, chunk, n);
                    int w = fs_.write(host, chunk, n);
                    if (w <= 0) break;
                    done += w;
                }
            }
            cpu_.setAx((uint16_t)done);
            ok();
            tracef("21 AH=%02X handle %u wanted %u got %u\n", ah, bx, cx, done);
            return true;
        }
        case 0x41: {
            std::string p = asciiz(ds, dx);
            int         r = fs_.remove(p);
            if (r < 0) fail(-r);
            else ok();
            tracef("21 AH=41 '%s' -> %d\n", p.c_str(), r);
            return true;
        }
        case 0x42: {
            int host = hostHandle(bx);
            if (host < 0) {
                if (bx < 5) {
                    cpu_.setAx(0);
                    cpu_.setDx(0);
                    ok();
                } else {
                    fail(kBadHandle);
                }
                return true;
            }
            int64_t off = (int32_t)((uint32_t)cx << 16 | dx);
            int64_t pos = fs_.seek(host, off, al);
            if (pos < 0) {
                fail(25);
                return true;
            }
            cpu_.setAx((uint16_t)pos);
            cpu_.setDx((uint16_t)(pos >> 16));
            ok();
            return true;
        }
        case 0x43: {
            std::string p = asciiz(ds, dx);
            FileInfo    fi;
            if (!fs_.stat(p, fi)) {
                fail(kNotFound);
            } else {
                if (al == 0) cpu_.setCx(fi.attr & 0x10 ? 0x10 : 0x20);
                ok();
            }
            tracef("21 AH=43 AL=%02X '%s'\n", al, p.c_str());
            return true;
        }
        case 0x44:  // IOCTL
            switch (al) {
                case 0x00:
                    if (bx < 5) cpu_.setDx(0x80D3);  // console: character device
                    else if (hostHandle(bx) >= 0) cpu_.setDx(0x0002);  // file on C:
                    else {
                        fail(kBadHandle);
                        return true;
                    }
                    ok();
                    return true;
                case 0x01:
                    ok();
                    return true;
                case 0x06:
                    cpu_.setAl(bx < 5 ? (con_ && con_->available() ? 0xFF : 0x00) : 0xFF);
                    ok();
                    return true;
                case 0x07:
                    cpu_.setAl(0xFF);
                    ok();
                    return true;
                case 0x08:
                    cpu_.setAx(1);  // fixed disk
                    ok();
                    return true;
                case 0x0E:
                    cpu_.setAl(0);
                    ok();
                    return true;
                default:
                    fail(kNoFunction);
                    tracef("21 AH=44 AL=%02X unsupported\n", al);
                    return true;
            }
        case 0x45:
        case 0x46: {
            int host = hostHandle(bx);
            if (bx < 5 && host < 0) {  // duplicating a console handle: the copy is the console too
                if (ah == 0x45) cpu_.setAx(bx);
                ok();
                return true;
            }
            if (host < 0) {
                fail(kBadHandle);
                return true;
            }
            // reopen is not possible portably; share the host handle (seek position is shared, as in DOS)
            if (ah == 0x45) {
                int h = newHandle(host, false);
                if (h < 0) fail(kTooMany);
                else {
                    cpu_.setAx((uint16_t)h);
                    ok();
                }
            } else {
                if (cx == bx) {
                    ok();
                    return true;
                }
                release(cx);  // 46h first closes whatever the target handle had open
                handles_[cx] = host;
                refs_[host]++;
                ok();
            }
            return true;
        }
        case 0x47: {
            std::string d = fs_.cwd();
            mem_.writeBlock(lin(ds, cpu_.si()), d.c_str(), d.size() + 1);
            ok();
            return true;
        }
        case 0x48: {
            uint16_t seg, largest;
            if (dm_.alloc(bx, psp_, seg, largest)) {
                cpu_.setAx(seg);
                ok();
            } else {
                fail(kNoMemory);
                cpu_.setBx(largest);
            }
            return true;
        }
        case 0x49:
            if (dm_.free(cpu_.es())) ok();
            else fail(kBadBlock);
            return true;
        case 0x4A: {
            uint16_t largest = 0;
            if (dm_.resize(cpu_.es(), bx, largest)) ok();
            else {
                fail(kNoMemory);
                cpu_.setBx(largest);
            }
            return true;
        }
        case 0x4C:
            exited_ = true;
            exitCode_ = al;
            cpu_.requestStop();  // nothing after the exit may run
            return true;
        case 0x4D:
            cpu_.setAx((uint16_t)exitCode_);
            return true;
        case 0x4E:
        case 0x4F: {
            uint32_t   dta = lin(dtaSeg_, dtaOff_);
            FindState& st = finds_[dta];
            if (ah == 0x4E) {
                std::string p = asciiz(ds, dx);
                fs_.find(p, (uint8_t)cx, st);
                tracef("21 AH=4E '%s' -> %d matches\n", p.c_str(), (int)st.matches.size());
            }
            if (st.next >= st.matches.size()) {
                fail(ah == 0x4E && st.matches.empty() ? kNotFound : kNoMoreFiles);
                if (ah == 0x4F) fail(kNoMoreFiles);
                return true;
            }
            const FileInfo& fi = st.matches[st.next++];
            mem_.write8(dta + 21, fi.attr);
            mem_.write16(dta + 22, fi.dosTime);
            mem_.write16(dta + 24, fi.dosDate);
            mem_.write16(dta + 26, (uint16_t)fi.size);
            mem_.write16(dta + 28, (uint16_t)(fi.size >> 16));
            std::string n = fi.name83.substr(0, 12);
            mem_.writeBlock(dta + 30, n.c_str(), n.size() + 1);
            ok();
            return true;
        }
        case 0x56: {
            std::string a = asciiz(ds, dx), b = asciiz(cpu_.es(), cpu_.di());
            int         r = fs_.rename(a, b);
            if (r < 0) fail(-r);
            else ok();
            tracef("21 AH=56 '%s' -> '%s' %d\n", a.c_str(), b.c_str(), r);
            return true;
        }
        case 0x57: {
            int host = hostHandle(bx);
            if (host < 0) {
                fail(kBadHandle);
                return true;
            }
            if (al == 0) {
                uint16_t d = 0, t = 0;
                fs_.fileTime(host, d, t);
                cpu_.setCx(t);
                cpu_.setDx(d);
            }
            ok();  // setting the stamp is accepted and ignored
            return true;
        }
        case 0x58:  // allocation strategy
            if (al == 0) cpu_.setAx(0);
            ok();
            return true;
        case 0x59:
            cpu_.setAx((uint16_t)lastError_);
            cpu_.setBh(1);
            cpu_.setBl(4);
            cpu_.setCh(1);
            return true;
        case 0x5C: {
            int host = hostHandle(bx);
            if (host < 0) {
                fail(kBadHandle);
                return true;
            }
            uint32_t off = (uint32_t)cx << 16 | dx, len = (uint32_t)cpu_.si() << 16 | cpu_.di();
            int      key = fs_.fileKey(host);
            bool     r = al == 0 ? locks_.lock(key, off, len, owner_) : locks_.unlock(key, off, len, owner_);
            if (r) ok();
            else fail(kLockViolation);
            return true;
        }
        case 0x62:
            cpu_.setBx(psp_);
            return true;
        case 0x63:  // DBCS table: none
            cpu_.setAl(0xFF);
            return true;
        case 0x66:  // code page
            cpu_.setBx(437);
            cpu_.setDx(437);
            ok();
            return true;
        case 0x67:  // set handle count: we have plenty
        case 0x68:  // commit file
            ok();
            return true;
        default:
            tracef("21 AH=%02X AL=%02X unsupported\n", ah, al);
            fail(kNoFunction);
            return true;
    }
}

}  // namespace door
}  // namespace axis
