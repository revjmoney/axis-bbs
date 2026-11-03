// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// One download or upload for a caller, run a slice at a time on their line (the host runs it like
// a door). Downloads read the file from its area; uploads are written under a new, clean name
// (never over an existing file), XMODEM padding is trimmed, and the description goes into FILES.BBS.
#pragma once
#include <memory>
#include <string>

#include "bbs/filelib.h"
#include "door/bytestream.h"
#include "net/transfer.h"

namespace axis::bbs {

struct TransferRequest {
    bool        upload = false;
    std::string area, name, desc;  // name: the file to send, or the name to save an XMODEM upload as
    char        proto = 'Z';       // Z ZMODEM, X XMODEM (CRC, 128), K XMODEM-1K
};

std::string cleanUploadName(const std::string& name);  // upper case, safe characters, no path

class TransferJob {
public:
    TransferJob(FileLibrary& lib, TransferRequest req, door::ByteStream& line);
    ~TransferJob();
    bool               step(uint32_t nowMs);  // false once finished
    bool               ok() const { return ok_; }
    uint32_t           bytes() const { return xfer_ ? xfer_->bytes() : 0; }
    uint32_t           size() const { return size_; }
    const std::string& savedName() const { return saved_; }
    const std::string& note() const { return note_; }  // why it failed, in plain words
    const TransferRequest& request() const { return req_; }

private:
    bool startFile(const std::string& name, uint32_t size);
    bool writeData(const uint8_t* p, size_t n);
    void endFile(bool complete);
    FileLibrary&                   lib_;
    TransferRequest                req_;
    door::ByteStream&              line_;
    std::unique_ptr<RandomFile>    src_;
    std::unique_ptr<WriteFile>     dst_;
    std::unique_ptr<net::Transfer> xfer_;
    std::string                    saved_, note_, held_;  // held_: 1Ah bytes that may be XMODEM padding
    uint32_t                       size_ = 0;
    bool                           ok_ = false, done_ = false, gotFile_ = false;
};

}  // namespace axis::bbs
