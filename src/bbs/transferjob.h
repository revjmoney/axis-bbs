// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// One download or upload for a caller, run a slice at a time on their line (the host runs it like
// a door). Downloads read the file from its area; uploads are written under a new, clean name
// (never over an existing file), XMODEM padding is trimmed, and the description goes into FILES.BBS.
#pragma once
#include <memory>
#include <string>

#include "bbs/features.h"
#include "bbs/filelib.h"
#include "bbs/uploadproc.h"
#include "door/bytestream.h"
#include "net/transfer.h"

namespace axis::bbs {

struct TransferRequest {
    bool        upload = false;
    std::string area, name, desc;  // name: the file to send, or the name to save an XMODEM upload as
    char        proto = 'Z';       // Z ZMODEM, X XMODEM (CRC, 128), K XMODEM-1K
};

std::string cleanUploadName(const std::string& name);  // upper case, safe characters, no path

#if AXIS_TRANSFERS
class TransferJob {
public:
    TransferJob(FileLibrary& lib, TransferRequest req, door::ByteStream& line, UploadInfo info = {});
#if AXIS_NETS
    // A download from a file that is already open (a Net Files fetch: req.area and req.name are only for the
    // host's log and screen). ready, when set, holds the sender until the data is there (transfer.h). The
    // senders' answer timers do not run while it waits; this job has no stall timer of its own.
    TransferJob(FileLibrary& lib, TransferRequest req, door::ByteStream& line, std::unique_ptr<RandomFile> file,
                uint32_t size, const std::string& name, net::Transfer::ReadyAt ready = nullptr);
#endif
    ~TransferJob();
    bool               step(uint32_t nowMs);  // false once finished
    // a ZIP upload has arrived and wants checking: the host lends the board's inflate window (owner: this job,
    // given back when the job is destroyed)
    bool needsWorkspace() const { return wantWorkspace_; }
    void beginProcessing(uint8_t* workspace, size_t size, uint8_t* zipState = nullptr);  // zipState: see UploadProcessor
    bool     waiting = false;  // the host: this ZIP check is waiting for the window, since waitSince (ms)
    uint32_t waitSince = 0;
    bool processing() const { return proc_ != nullptr; }
    double processProgress() const { return proc_ ? proc_->progress() : 0; }
    bool               ok() const { return ok_; }
    uint32_t           bytes() const { return xfer_ ? xfer_->bytes() : 0; }
    uint32_t           size() const { return size_; }
    const std::string& savedName() const { return saved_; }
    const std::string& note() const { return note_; }  // why it failed, in plain words
    const TransferRequest& request() const { return req_; }

private:
    void startSender(const std::string& name);  // a download: src_ and size_ are set
    bool startFile(const std::string& name, uint32_t size);
    bool writeData(const uint8_t* p, size_t n);
    void endFile(bool complete);
    void finish();  // list the upload (or not) and settle ok_/note_
    void statusLine();
    FileLibrary&                   lib_;
    TransferRequest                req_;
    door::ByteStream&              line_;
    std::unique_ptr<RandomFile>    src_;
    std::unique_ptr<WriteFile>     dst_;
    std::unique_ptr<net::Transfer> xfer_;
    std::unique_ptr<UploadProcessor> proc_;
    UploadInfo                     info_;
    std::string                    saved_, note_, held_;  // held_: 1Ah bytes that may be XMODEM padding
    uint32_t                       size_ = 0;
    bool                           ok_ = false, done_ = false, gotFile_ = false, kept_ = false, wantWorkspace_ = false;
    uint32_t                       lastDraw_ = 0;
};
#else
// Downloads and uploads are left out of this build (features.h): transferjob.cpp, uploadproc.cpp and XMODEM/ZMODEM
// compile to nothing. The Files menu offers no transfer, so the host never makes one of these.
class TransferJob {
public:
    bool                   step(uint32_t) { return false; }
    bool                   needsWorkspace() const { return false; }
    void                   beginProcessing(uint8_t*, size_t, uint8_t* = nullptr) {}
    bool                   waiting = false;
    uint32_t               waitSince = 0;
    bool                   ok() const { return false; }
    uint32_t               bytes() const { return 0; }
    uint32_t               size() const { return 0; }
    const std::string&     savedName() const { return note_; }
    const std::string&     note() const { return note_; }
    const TransferRequest& request() const { return req_; }

private:
    TransferRequest req_;
    std::string     note_;
};
#endif

}  // namespace axis::bbs
