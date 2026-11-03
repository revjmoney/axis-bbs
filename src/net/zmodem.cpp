// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "net/zmodem.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "net/crc.h"

namespace axis::net {

namespace {
constexpr uint8_t ZPAD = '*', ZDLE = 0x18, ZBIN = 'A', ZHEX = 'B', ZBIN32 = 'C', XON = 0x11;
constexpr uint8_t ZCRCE = 'h', ZCRCG = 'i', ZCRCQ = 'j', ZCRCW = 'k', ZRUB0 = 'l', ZRUB1 = 'm';
enum : uint8_t {
    ZRQINIT = 0, ZRINIT, ZSINIT, ZACK, ZFILE, ZSKIP, ZNAK, ZABORT, ZFIN, ZRPOS, ZDATA, ZEOF, ZFERR, ZCRC,
    ZCHALLENGE, ZCOMPL, ZCAN, ZFREECNT, ZCOMMAND, ZSTDERR
};
constexpr uint8_t CANFDX = 0x01, CANOVIO = 0x02, CANFC32 = 0x20, ESCCTL = 0x40;
constexpr size_t  kBlock = 1024;
constexpr int     kBlocksPerStep = 8;

// every control character, the C1 range, DEL and 0xFF go out as ZDLE pairs
void escapeInto(std::string& s, uint8_t c) {
    if (c == 0x7F) s += {(char)ZDLE, (char)ZRUB0};
    else if (c == 0xFF) s += {(char)ZDLE, (char)ZRUB1};
    else if (c < 0x20 || (c >= 0x80 && c < 0xA0)) s += {(char)ZDLE, (char)(c ^ 0x40)};
    else s += (char)c;
}

void hexHeader(door::ByteStream& line, uint8_t type, const uint8_t h[4]) {
    uint8_t  b[5] = {type, h[0], h[1], h[2], h[3]};
    uint16_t crc = crc16Xmodem(0, b, 5);
    char     s[40];
    int n = std::snprintf(s, sizeof s, "**%cB%02x%02x%02x%02x%02x%02x%02x\r\n", ZDLE, type, h[0], h[1], h[2], h[3], crc >> 8,
                          crc & 0xFF);
    std::string out(s, (size_t)n);
    out[n - 1] = '\x8a';  // lrzsz ends hex headers with CR and LF|80h
    if (type != ZFIN && type != ZACK) out += (char)XON;
    line.write((const uint8_t*)out.data(), out.size());
}

void binHeader(door::ByteStream& line, uint8_t type, const uint8_t h[4], bool crc32) {
    uint8_t     b[5] = {type, h[0], h[1], h[2], h[3]};
    std::string s = {(char)ZPAD, (char)ZDLE, (char)(crc32 ? ZBIN32 : ZBIN)};
    for (uint8_t c : b) escapeInto(s, c);
    if (crc32) {
        uint32_t crc = crc32Update(0xFFFFFFFFu, b, 5);
        crc = ~crc;
        for (int i = 0; i < 4; i++) escapeInto(s, (uint8_t)(crc >> (8 * i)));
    } else {
        uint16_t crc = crc16Xmodem(0, b, 5);
        escapeInto(s, (uint8_t)(crc >> 8));
        escapeInto(s, (uint8_t)crc);
    }
    line.write((const uint8_t*)s.data(), s.size());
}

void subpacket(door::ByteStream& line, const uint8_t* p, size_t n, uint8_t end, bool crc32) {
    std::string s;
    s.reserve(n + n / 8 + 16);
    for (size_t i = 0; i < n; i++) escapeInto(s, p[i]);
    s += {(char)ZDLE, (char)end};
    if (crc32) {
        uint32_t crc = crc32Update(0xFFFFFFFFu, p, n);
        crc = ~crc32Update(crc, &end, 1);
        for (int i = 0; i < 4; i++) escapeInto(s, (uint8_t)(crc >> (8 * i)));
    } else {
        uint16_t crc = crc16Xmodem(crc16Xmodem(0, p, n), &end, 1);
        escapeInto(s, (uint8_t)(crc >> 8));
        escapeInto(s, (uint8_t)crc);
    }
    if (end == ZCRCW) s += (char)XON;
    line.write((const uint8_t*)s.data(), s.size());
}

void posHeader(uint8_t h[4], uint32_t pos) {
    for (int i = 0; i < 4; i++) h[i] = (uint8_t)(pos >> (8 * i));
}

void cancel(door::ByteStream& line) {
    uint8_t c[] = {ZDLE, ZDLE, ZDLE, ZDLE, ZDLE, ZDLE, ZDLE, ZDLE, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};
    line.write(c, sizeof c);
}

int hexVal(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
}  // namespace

// ------------------------------------------------------------------ frame reader
bool ZReader::unescape(uint8_t c, uint8_t& out, bool& isEnd) {
    isEnd = false;
    if (!zdle_) {
        if (c == ZDLE) {
            zdle_ = true;
            return false;
        }
        out = c;
        return true;
    }
    zdle_ = false;
    if (c >= ZCRCE && c <= ZCRCW) {
        isEnd = true;
        out = c;
    } else if (c == ZRUB0) out = 0x7F;
    else if (c == ZRUB1) out = 0xFF;
    else out = c ^ 0x40;
    return true;
}

ZReader::Event ZReader::feed(uint8_t c) {
    cans_ = c == ZDLE ? cans_ + 1 : 0;
    if (cans_ >= 5) {
        cans_ = 0;
        mode_ = Mode::Seek;
        return Event::Cancel;
    }
    if ((c & 0x7F) == XON || (c & 0x7F) == 0x13) return Event::None;  // flow control: always escaped in data
    uint8_t v;
    bool    isEnd;
    switch (mode_) {
        case Mode::Seek:
            if (c == ZPAD) mode_ = Mode::Pad;
            return Event::None;
        case Mode::Pad:
            if (c == ZDLE) mode_ = Mode::Kind;
            else if (c != ZPAD) mode_ = Mode::Seek;
            return Event::None;
        case Mode::Kind:
            n_ = 0;
            zdle_ = false;
            if (c == ZHEX) mode_ = Mode::Hex;
            else if (c == ZBIN || c == ZBIN32) {
                crc32_ = c == ZBIN32;
                need_ = crc32_ ? 9 : 7;
                mode_ = Mode::Bin;
            } else mode_ = Mode::Seek;
            return Event::None;
        case Mode::Hex: {
            hex_[n_++] = (char)c;
            if (n_ < 14) return Event::None;
            mode_ = Mode::Seek;
            for (int i = 0; i < 7; i++) {
                int a = hexVal(hex_[2 * i]), b = hexVal(hex_[2 * i + 1]);
                if (a < 0 || b < 0) return Event::None;
                buf_[i] = (uint8_t)(a << 4 | b);
            }
            if (crc16Xmodem(0, buf_, 5) != (uint16_t)(buf_[5] << 8 | buf_[6])) return Event::None;
            crc32_ = false;  // data after a hex header uses CRC-16
            break;
        }
        case Mode::Bin:
            if (!unescape(c, v, isEnd) || isEnd) return Event::None;
            buf_[n_++] = v;
            if (n_ < need_) return Event::None;
            mode_ = Mode::Seek;
            if (crc32_) {
                uint32_t crc = ~crc32Update(0xFFFFFFFFu, buf_, 5);
                if (crc != (uint32_t)(buf_[5] | buf_[6] << 8 | buf_[7] << 16 | (uint32_t)buf_[8] << 24)) return Event::None;
            } else if (crc16Xmodem(0, buf_, 5) != (uint16_t)(buf_[5] << 8 | buf_[6])) {
                return Event::None;
            }
            break;
        case Mode::Data:
            if (!unescape(c, v, isEnd)) return Event::None;
            if (isEnd) {
                end = v;
                n_ = 0;
                need_ = crc32_ ? 4 : 2;
                mode_ = Mode::DataCrc;
            } else if (data.size() < kMaxSubpacket) {
                data.push_back(v);
            } else {  // runaway subpacket: report it as bad
                end = ZCRCE;
                crcOk = false;
                mode_ = Mode::Seek;
                return Event::Subpacket;
            }
            return Event::None;
        case Mode::DataCrc:
            if (!unescape(c, v, isEnd) || isEnd) return Event::None;
            buf_[n_++] = v;
            if (n_ < need_) return Event::None;
            if (crc32_) {
                uint32_t crc = crc32Update(0xFFFFFFFFu, data.data(), data.size());
                crc = ~crc32Update(crc, &end, 1);
                crcOk = crc == (uint32_t)(buf_[0] | buf_[1] << 8 | buf_[2] << 16 | (uint32_t)buf_[3] << 24);
            } else {
                uint16_t crc = crc16Xmodem(crc16Xmodem(0, data.data(), data.size()), &end, 1);
                crcOk = crc == (uint16_t)(buf_[0] << 8 | buf_[1]);
            }
            mode_ = end == ZCRCW || end == ZCRCE || !crcOk ? Mode::Seek : Mode::Data;
            return Event::Subpacket;
    }
    // a header arrived
    type = buf_[0];
    std::memcpy(h, buf_ + 1, 4);
    if (type == ZDATA || type == ZFILE || type == ZSINIT || type == ZCOMMAND) {
        data.clear();
        mode_ = Mode::Data;
    }
    return Event::Header;
}

// ------------------------------------------------------------------ sender
void ZmodemSender::sendFile() {
    uint8_t h[4] = {0, 0, 0, 1};  // ZF0 = ZCBIN: binary, no conversion
    binHeader(line_, ZFILE, h, crc32_);
    char        info[48];
    int         n = std::snprintf(info, sizeof info, "%lu 0 0 0 1 %lu", (unsigned long)size_, (unsigned long)size_);
    std::string payload = name_;
    payload += '\0';
    payload.append(info, (size_t)n);
    payload += '\0';
    subpacket(line_, (const uint8_t*)payload.data(), payload.size(), ZCRCW, crc32_);
}

void ZmodemSender::sendData(uint32_t now) {
    uint8_t buf[kBlock];
    for (int k = 0; k < kBlocksPerStep && st_ == St::Streaming; k++) {
        uint32_t n = size_ - pos_ < kBlock ? size_ - pos_ : (uint32_t)kBlock;
        if (n && !read_(pos_, buf, n)) {
            cancel(line_);
            finish(Result::Failed, "read error");
            return;
        }
        bool last = pos_ + n >= size_;
        subpacket(line_, buf, n, last ? ZCRCE : ZCRCG, crc32_);
        pos_ += n;
        bytes_ = pos_;
        if (last) {
            uint8_t h[4];
            posHeader(h, size_);
            hexHeader(line_, ZEOF, h);
            st_ = St::WaitEofAck;
            since_ = now;
            tries_ = 0;
        }
    }
}

bool ZmodemSender::step(uint32_t now) {
    if (result_ != Result::Running) return false;
    if (!line_.carrier()) return finish(Result::Cancelled, "hung up");
    uint8_t zero[4] = {0, 0, 0, 0};
    if (st_ == St::Start) {
        line_.write((const uint8_t*)"rz\r", 3);
        hexHeader(line_, ZRQINIT, zero);
        st_ = St::WaitRinit;
        since_ = now;
    }
    while (line_.available() > 0 && result_ == Result::Running) {
        ZReader::Event e = in_.feed((uint8_t)line_.read());
        if (e == ZReader::Event::Cancel) return finish(Result::Cancelled, "cancelled by the caller");
        if (e == ZReader::Event::Subpacket) continue;  // a ZSINIT from an odd receiver: ignore
        if (e != ZReader::Event::Header) continue;
        uint8_t t = in_.type;
        if (t == ZCAN || t == ZABORT || t == ZFERR) return finish(Result::Cancelled, "the caller's program gave up");
        if (t == ZSKIP) return finish(Result::Failed, "the caller's program skipped the file");
        since_ = now;
        if (t == ZRINIT) {
            if (st_ == St::WaitRinit) {  // a repeat ZRINIT while waiting for ZRPOS is ignored (the timeout covers a lost one)
                crc32_ = (in_.h[3] & CANFC32) != 0;
                sendFile();
                st_ = St::WaitRpos;
            } else if (st_ == St::WaitEofAck) {
                hexHeader(line_, ZFIN, zero);
                st_ = St::WaitFin;
            }
        } else if (t == ZRPOS && (st_ == St::WaitRpos || st_ == St::Streaming || st_ == St::WaitEofAck)) {
            pos_ = in_.pos() <= size_ ? in_.pos() : size_;
            uint8_t h[4];
            posHeader(h, pos_);
            binHeader(line_, ZDATA, h, crc32_);
            st_ = St::Streaming;
        } else if (t == ZFIN && st_ == St::WaitFin) {
            line_.write((const uint8_t*)"OO", 2);
            return finish(Result::Ok);
        }
    }
    if (result_ != Result::Running) return false;
    if (st_ == St::Streaming) {
        sendData(now);
        return result_ == Result::Running;
    }
    if (now - since_ > (st_ == St::WaitRinit ? 5000u : 10000u)) {
        since_ = now;
        if (st_ == St::WaitFin) return finish(Result::Ok);  // the file is through; the ZFIN reply got lost
        if (++tries_ > 10) {
            cancel(line_);
            return finish(Result::Failed, "the caller's program didn't answer");
        }
        if (st_ == St::WaitRinit) hexHeader(line_, ZRQINIT, zero);
        else if (st_ == St::WaitRpos) sendFile();
        else if (st_ == St::WaitEofAck) {
            uint8_t h[4];
            posHeader(h, size_);
            hexHeader(line_, ZEOF, h);
        }
    }
    return true;
}

// ------------------------------------------------------------------ receiver
void ZmodemReceiver::sendRinit() {
    uint8_t h[4] = {0, 0, 0, CANFDX | CANOVIO | CANFC32 | ESCCTL};  // full streaming, CRC-32, escape controls
    hexHeader(line_, ZRINIT, h);
}

bool ZmodemReceiver::step(uint32_t now) {
    if (result_ != Result::Running) return false;
    if (!line_.carrier()) {
        if (inFile_) onEnd_(false);
        return finish(Result::Cancelled, "hung up");
    }
    if (!started_) {
        started_ = true;
        sendRinit();
        since_ = now;
    }
    uint8_t zero[4] = {0, 0, 0, 0};
    while (line_.available() > 0 && result_ == Result::Running) {
        ZReader::Event e = in_.feed((uint8_t)line_.read());
        if (e == ZReader::Event::None) continue;
        since_ = now;
        tries_ = 0;
        if (e == ZReader::Event::Cancel) {
            if (inFile_) onEnd_(false);
            return finish(Result::Cancelled, "cancelled by the caller");
        }
        if (e == ZReader::Event::Subpacket) {
            if (wantSinit_) {
                wantSinit_ = false;
                hexHeader(line_, ZACK, zero);
                continue;
            }
            if (wantFileInfo_) {
                wantFileInfo_ = false;
                if (!in_.crcOk) {
                    hexHeader(line_, ZNAK, zero);
                    continue;
                }
                std::string name((const char*)in_.data.data(), strnlen((const char*)in_.data.data(), in_.data.size()));
                size_t      at = name.size() + 1;
                uint32_t    size = at < in_.data.size() ? (uint32_t)std::strtoul((const char*)in_.data.data() + at, nullptr, 10) : 0;
                size_t      slash = name.find_last_of("/\\");
                if (slash != std::string::npos) name = name.substr(slash + 1);  // a name, never a path
                if (onFile_(name, size)) {
                    inFile_ = true;
                    pos_ = 0;
                    files_++;
                    uint8_t h[4];
                    posHeader(h, 0);
                    hexHeader(line_, ZRPOS, h);
                } else {
                    hexHeader(line_, ZSKIP, zero);
                }
                continue;
            }
            if (!inFile_ || discard_) continue;
            if (!in_.crcOk) {  // ask for it again from where we are
                discard_ = true;
                in_.expectHeader();
                uint8_t h[4];
                posHeader(h, pos_);
                hexHeader(line_, ZRPOS, h);
                continue;
            }
            if (!in_.data.empty() && !write_(in_.data.data(), in_.data.size())) {
                cancel(line_);
                onEnd_(false);
                return finish(Result::Failed, "couldn't save the file");
            }
            pos_ += (uint32_t)in_.data.size();
            bytes_ += (uint32_t)in_.data.size();
            in_.data.clear();
            if (in_.end == ZCRCW || in_.end == ZCRCQ) {
                uint8_t h[4];
                posHeader(h, pos_);
                hexHeader(line_, ZACK, h);
            }
            continue;
        }
        // a header
        switch (in_.type) {
            case ZRQINIT:
                sendRinit();
                break;
            case ZSINIT:
                wantSinit_ = true;
                break;
            case ZFILE:
                wantFileInfo_ = true;
                break;
            case ZDATA:
                if (!inFile_) {
                    in_.expectHeader();
                    break;
                }
                discard_ = in_.pos() != pos_;
                if (discard_) {
                    in_.expectHeader();
                    uint8_t h[4];
                    posHeader(h, pos_);
                    hexHeader(line_, ZRPOS, h);
                }
                break;
            case ZEOF:
                if (inFile_ && in_.pos() == pos_) {
                    inFile_ = false;
                    onEnd_(true);
                    sendRinit();
                }
                break;
            case ZFIN:
                if (inFile_) onEnd_(false);
                inFile_ = false;
                hexHeader(line_, ZFIN, zero);
                return finish(files_ ? Result::Ok : Result::Failed, files_ ? "" : "no file was sent");
            case ZCAN:
            case ZABORT:
                if (inFile_) onEnd_(false);
                return finish(Result::Cancelled, "cancelled by the caller");
            default:
                break;
        }
    }
    if (result_ != Result::Running) return false;
    if (now - since_ > 10000) {
        since_ = now;
        if (++tries_ > 6) {
            cancel(line_);
            if (inFile_) onEnd_(false);
            return finish(Result::Failed, "the transfer stalled");
        }
        if (inFile_) {
            uint8_t h[4];
            posHeader(h, pos_);
            hexHeader(line_, ZRPOS, h);
        } else {
            sendRinit();
        }
    }
    return true;
}

}  // namespace axis::net
