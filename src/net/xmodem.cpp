// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "net/xmodem.h"

#include "net/crc.h"

namespace axis::net {

namespace {
constexpr uint8_t SOH = 0x01, STX = 0x02, EOT = 0x04, ACK = 0x06, NAK = 0x15, CAN = 0x18, SUB = 0x1A;
constexpr uint32_t kStartWait = 60000, kAckWait = 10000, kPoke = 3000, kIdle = 30000;
constexpr int      kTries = 10;

void cancel(door::ByteStream& line) {
    uint8_t c[] = {CAN, CAN, CAN, CAN, CAN, 8, 8, 8, 8, 8};
    line.write(c, sizeof c);
}
}  // namespace

// ------------------------------------------------------------------ sender
void XmodemSender::sendBlock() {
    blockLen_ = oneK_ && crc_ && size_ - pos_ > 128 ? 1024 : 128;
    std::vector<uint8_t> b(3 + blockLen_ + 2, SUB);
    b[0] = blockLen_ == 1024 ? STX : SOH;
    b[1] = seq_;
    b[2] = (uint8_t)~seq_;
    uint32_t have = size_ - pos_ < blockLen_ ? size_ - pos_ : blockLen_;
    if (have && !read_(pos_, b.data() + 3, have)) {
        cancel(line_);
        finish(Result::Failed, "read error");
        return;
    }
    size_t n = 3 + blockLen_;
    if (crc_) {
        uint16_t c = crc16Xmodem(0, b.data() + 3, blockLen_);
        b[n++] = (uint8_t)(c >> 8);
        b[n++] = (uint8_t)c;
    } else {
        uint8_t sum = 0;
        for (uint32_t i = 0; i < blockLen_; i++) sum += b[3 + i];
        b[n++] = sum;
    }
    line_.write(b.data(), n);
}

bool XmodemSender::step(uint32_t now) {
    if (result_ != Result::Running) return false;
    if (!started_) {
        started_ = true;
        since_ = now;
    }
    if (!line_.carrier()) return finish(Result::Cancelled, "hung up");
    while (line_.available() > 0 && result_ == Result::Running) {
        int c = line_.read();
        cans_ = c == CAN ? cans_ + 1 : 0;
        if (cans_ >= 2) return finish(Result::Cancelled, "cancelled by the caller");
        switch (st_) {
            case St::WaitStart:
                if (c == 'C' || c == NAK) {
                    crc_ = c == 'C';
                    if (size_ == 0) {
                        uint8_t e = EOT;
                        line_.write(&e, 1);
                        st_ = St::WaitEotAck;
                    } else {
                        sendBlock();
                        st_ = St::WaitAck;
                    }
                    since_ = now;
                    tries_ = 0;
                }
                break;
            case St::WaitAck:
                if (c == ACK) {
                    uint32_t moved = size_ - pos_ < blockLen_ ? size_ - pos_ : blockLen_;
                    pos_ += moved;
                    bytes_ = pos_;
                    seq_++;
                    tries_ = 0;
                    since_ = now;
                    if (pos_ >= size_) {
                        uint8_t e = EOT;
                        line_.write(&e, 1);
                        st_ = St::WaitEotAck;
                    } else {
                        sendBlock();
                    }
                } else if (c == NAK || c == 'C') {
                    if (++tries_ > kTries) {
                        cancel(line_);
                        return finish(Result::Failed, "too many errors");
                    }
                    sendBlock();
                    since_ = now;
                }
                break;
            case St::WaitEotAck:
                if (c == ACK) return finish(Result::Ok);
                if (c == NAK) {
                    uint8_t e = EOT;
                    line_.write(&e, 1);
                }
                break;
        }
    }
    if (result_ != Result::Running) return false;
    uint32_t wait = st_ == St::WaitStart ? kStartWait : kAckWait;
    if (now - since_ > wait) {
        if (st_ == St::WaitStart || ++tries_ > kTries) {
            cancel(line_);
            return finish(Result::Failed, "the caller's program didn't answer");
        }
        if (st_ == St::WaitAck) sendBlock();
        else {
            uint8_t e = EOT;
            line_.write(&e, 1);
        }
        since_ = now;
    }
    return true;
}

// ------------------------------------------------------------------ receiver
bool XmodemReceiver::step(uint32_t now) {
    if (result_ != Result::Running) return false;
    if (!started_) {
        started_ = true;
        since_ = lastByte_ = now - kPoke - 1;  // poke at once
    }
    if (!line_.carrier()) return finish(Result::Cancelled, "hung up");
    while (line_.available() > 0) {
        buf_.push_back((uint8_t)line_.read());
        lastByte_ = now;
    }
    for (;;) {
        if (buf_.empty()) break;
        uint8_t h = buf_[0];
        if (h == EOT) {
            uint8_t a = ACK;
            line_.write(&a, 1);
            return finish(Result::Ok);
        }
        if (h == CAN) {
            if (buf_.size() < 2) break;
            if (buf_[1] == CAN) return finish(Result::Cancelled, "cancelled by the caller");
            buf_.erase(buf_.begin());
            continue;
        }
        if (h != SOH && h != STX) {  // noise between blocks
            buf_.erase(buf_.begin());
            continue;
        }
        size_t len = h == STX ? 1024 : 128, need = 3 + len + (crc_ ? 2 : 1);
        if (buf_.size() < need) break;
        bool ok = (uint8_t)(buf_[1] ^ buf_[2]) == 0xFF;
        if (ok && crc_) ok = crc16Xmodem(0, buf_.data() + 3, len) == (uint16_t)(buf_[3 + len] << 8 | buf_[4 + len]);
        if (ok && !crc_) {
            uint8_t sum = 0;
            for (size_t i = 0; i < len; i++) sum += buf_[3 + i];
            ok = sum == buf_[3 + len];
        }
        uint8_t reply = NAK;
        if (ok && buf_[1] == expect_) {
            if (!write_(buf_.data() + 3, len)) {
                cancel(line_);
                return finish(Result::Failed, "couldn't save the file");
            }
            bytes_ += (uint32_t)len;
            expect_++;
            errors_ = 0;
            gotBlock_ = true;
            reply = ACK;
        } else if (ok && buf_[1] == (uint8_t)(expect_ - 1)) {
            reply = ACK;  // a repeat of the last block: our ACK was lost
        } else if (++errors_ > kTries) {
            cancel(line_);
            return finish(Result::Failed, "too many errors");
        }
        buf_.erase(buf_.begin(), buf_.begin() + (long)need);
        line_.write(&reply, 1);
        since_ = now;
    }
    if (!gotBlock_ && now - since_ > kPoke) {  // ask the sender to start: CRC first, then checksum
        if (++pokes_ > 4) crc_ = false;
        if (pokes_ > 20) return finish(Result::Failed, "nothing was sent");
        uint8_t c = crc_ ? 'C' : NAK;
        line_.write(&c, 1);
        since_ = now;
    } else if (gotBlock_ && !buf_.empty() && now - lastByte_ > 1000) {  // a block stalled halfway
        buf_.clear();
        uint8_t n = NAK;
        line_.write(&n, 1);
        lastByte_ = now;
    }
    if (gotBlock_ && now - since_ > kIdle) {
        cancel(line_);
        return finish(Result::Failed, "the transfer stalled");
    }
    return true;
}

}  // namespace axis::net
