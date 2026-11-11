// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// ZMODEM (Chuck Forsberg's protocol, as lrzsz and SyncTERM speak it), both directions.
// We always ask for, and send, every control character escaped, so a telnet client's CR and NUL
// handling can't touch the data; 0xFF is escaped too. CRC-32 frames when the other side can.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "net/transfer.h"

namespace axis::net {

// Decodes what arrives: headers (hex, binary CRC-16, binary CRC-32) and data subpackets.
class ZReader {
public:
    enum class Event { None, Header, Subpacket, Cancel };
    Event feed(uint8_t c);
    void  expectHeader() { mode_ = Mode::Seek; }  // after ZCRCW/ZCRCE, or to skip a bad subpacket
    // the last header
    uint8_t  type = 0, h[4] = {0, 0, 0, 0};
    uint32_t pos() const { return h[0] | h[1] << 8 | h[2] << 16 | (uint32_t)h[3] << 24; }
    // the last subpacket
    std::vector<uint8_t> data;
    uint8_t              end = 0;
    bool                 crcOk = false;
    static constexpr size_t kMaxSubpacket = 8192;

private:
    enum class Mode { Seek, Pad, Kind, Hex, Bin, Data, DataCrc };
    bool    unescape(uint8_t c, uint8_t& out, bool& isEnd);  // false while a ZDLE waits for its pair
    Mode    mode_ = Mode::Seek;
    bool    crc32_ = false, zdle_ = false;
    int     cans_ = 0;
    uint8_t buf_[16] = {};
    size_t  n_ = 0, need_ = 0;
    char    hex_[14] = {};
};

class ZmodemSender : public Transfer {
public:
    ZmodemSender(door::ByteStream& line, ReadAt read, uint32_t size, std::string name)
        : line_(line), read_(std::move(read)), size_(size), name_(std::move(name)) {}
    bool step(uint32_t nowMs) override;

private:
    enum class St { Start, WaitRinit, WaitRpos, Streaming, WaitEofAck, WaitFin };
    void sendFile();
    void sendData(uint32_t nowMs);
    door::ByteStream& line_;
    ReadAt            read_;
    uint32_t          size_, pos_ = 0;
    std::string       name_;
    ZReader           in_;
    St                st_ = St::Start;
    bool              crc32_ = true;
    uint32_t          since_ = 0;
    int               tries_ = 0;
};

class ZmodemReceiver : public Transfer {
public:
    ZmodemReceiver(door::ByteStream& line, OnFile onFile, Write write, OnEnd onEnd)
        : line_(line), onFile_(std::move(onFile)), write_(std::move(write)), onEnd_(std::move(onEnd)) {}
    bool step(uint32_t nowMs) override;
    int  files() const { return files_; }

private:
    void sendRinit();
    door::ByteStream& line_;
    OnFile            onFile_;
    Write             write_;
    OnEnd             onEnd_;
    ZReader           in_;
    bool              started_ = false, inFile_ = false, discard_ = false, wantFileInfo_ = false, wantSinit_ = false;
    uint32_t          pos_ = 0, since_ = 0;
    int               tries_ = 0, files_ = 0;
};

}  // namespace axis::net
