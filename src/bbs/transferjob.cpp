// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/transferjob.h"

#include <cctype>
#include <cstdio>

#include "bbs/theme.h"

#include "net/xmodem.h"
#include "net/zmodem.h"

namespace axis::bbs {

std::string cleanUploadName(const std::string& in) {
    size_t      slash = in.find_last_of("/\\:");
    std::string base = slash == std::string::npos ? in : in.substr(slash + 1);
    std::string out;
    for (char c : base) {
        unsigned char u = (unsigned char)c;
        if (std::isalnum(u) || c == '-' || c == '_') out += (char)std::toupper(u);
        else if (c == '.') out += '.';
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out[0] == '.') out.erase(0, 1);  // no hidden files, no ".."
    if (out.size() > 40) out = out.substr(0, 36) + out.substr(out.size() - 4);
    if (out == "FILES.BBS") out = "FILES_.BBS";
    return out;
}

TransferJob::TransferJob(FileLibrary& lib, TransferRequest req, door::ByteStream& line, UploadInfo info)
    : lib_(lib), req_(std::move(req)), line_(line), info_(std::move(info)) {
    if (!req_.upload) {
        src_ = lib_.open(req_.area, req_.name);
        if (!src_) {
            note_ = "that file isn't there any more";
            done_ = true;
            return;
        }
        size_ = src_->size();
        RandomFile* f = src_.get();
        auto        rd = [f](uint32_t off, uint8_t* buf, size_t n) { return f->readAt(off, buf, n); };
        if (req_.proto == 'Z') xfer_.reset(new net::ZmodemSender(line_, rd, size_, req_.name));
        else xfer_.reset(new net::XmodemSender(line_, rd, size_, req_.proto == 'K'));
        return;
    }
    auto wr = [this](const uint8_t* p, size_t n) { return writeData(p, n); };
    if (req_.proto == 'Z') {
        xfer_.reset(new net::ZmodemReceiver(
            line_, [this](const std::string& n, uint32_t s) { return startFile(n, s); }, wr, [this](bool ok) { endFile(ok); }));
    } else {
        if (!startFile(req_.name, 0)) {
            done_ = true;
            return;
        }
        xfer_.reset(new net::XmodemReceiver(line_, wr));
    }
}

TransferJob::~TransferJob() {
    if (dst_) dst_->finish(false);  // an unfinished upload leaves nothing behind
}

bool TransferJob::startFile(const std::string& name, uint32_t size) {
    if (gotFile_) return false;  // one file per upload: the description belongs to it
    std::string clean = cleanUploadName(name);
    if (clean.empty()) {
        note_ = "the file name wasn't usable";
        return false;
    }
    std::string base = clean, ext;
    size_t      dot = clean.rfind('.');
    if (dot != std::string::npos) {
        base = clean.substr(0, dot);
        ext = clean.substr(dot);
    }
    auto taken = [&](const std::string& n) {
        for (auto& f : lib_.list(req_.area))
            if (f.name == n) return true;
        return false;
    };
    for (int i = 1; taken(clean) && i < 100; i++) clean = base + "-" + std::to_string(i) + ext;
    dst_ = lib_.create(req_.area, clean);
    if (!dst_) {
        note_ = "this area doesn't take uploads right now";
        return false;
    }
    saved_ = clean;
    size_ = size;
    gotFile_ = true;
    return true;
}

bool TransferJob::writeData(const uint8_t* p, size_t n) {
    if (!dst_) return false;
    if (req_.proto == 'Z') return dst_->write(p, n);
    // XMODEM pads its last block with 1Ah: hold a trailing run back until something else follows
    size_t end = n;
    while (end > 0 && p[end - 1] == 0x1A) end--;
    if (end > 0) {
        if (!held_.empty() && !dst_->write((const uint8_t*)held_.data(), held_.size())) return false;
        held_.clear();
        if (!dst_->write(p, end)) return false;
    }
    held_.append((const char*)p + end, n - end);
    return true;
}

void TransferJob::endFile(bool complete) {
    if (!dst_) return;
    bool kept = complete && dst_->finish(true);
    if (!complete) dst_->finish(false);
    dst_.reset();
    kept_ = kept;  // listed in finish(), once a ZIP has passed its check
}

void TransferJob::finish() {
    done_ = true;
    if (!req_.upload || !kept_) return;
    if (proc_ && proc_->result() == UploadProcessor::Result::Rejected) {
        ok_ = false;
        note_ = proc_->note();
        return;
    }
    lib_.describe(req_.area, saved_, req_.desc.empty() ? "(uploaded, no description)" : req_.desc);
    ok_ = true;
    if (proc_) note_ = proc_->note();
}

void TransferJob::beginProcessing(uint8_t* workspace, size_t size) {
    wantWorkspace_ = false;
    proc_.reset(new UploadProcessor(lib_, req_.area, saved_, info_, workspace, size));
    std::string s = "\r\n\r\n";
    s += theme::window("PROCESSING UPLOAD", {{"file", saved_},
                                             {"step 1", "unzip, test every file's CRC"},
                                             {"step 2", std::string("add ") + UploadProcessor::kInfoName},
                                             {"step 3", "rezip as a standard ZIP"}});
    s += "\r\n";
    line_.write((const uint8_t*)s.data(), s.size());
    statusLine();
}

void TransferJob::statusLine() {  // one line, redrawn in place
    const char* what = !proc_ ? "" : proc_->stage() == UploadProcessor::Stage::Check ? "testing "
                                 : proc_->stage() == UploadProcessor::Stage::Write ? "rezipping "
                                 : proc_->stage() == UploadProcessor::Stage::Info  ? "adding "
                                                                                    : "finishing ";
    char pct[16];
    std::snprintf(pct, sizeof pct, " %3d%%  ", (int)(processProgress() * 100 + 0.5));
    std::string name = proc_ ? proc_->current() : "";
    if (name.size() > 24) name = name.substr(name.size() - 24);
    std::string s = "\r\x1b[K  " + theme::bar(processProgress(), 30) + theme::WHITE + pct + theme::TEXT + what + name;
    line_.write((const uint8_t*)s.data(), s.size());
}

bool TransferJob::step(uint32_t now) {
    if (done_) return false;
    if (proc_) {
        bool running = true;
        for (int i = 0; i < 4 && running; i++) running = proc_->step();
        if (running) {
            if (now - lastDraw_ > 150) {
                lastDraw_ = now;
                statusLine();
            }
            return true;
        }
        statusLine();
        finish();
        return false;
    }
    if (wantWorkspace_) return true;  // the host lends memory, then calls beginProcessing
    if (!xfer_) return false;
    if (xfer_->step(now)) return true;
    bool good = xfer_->result() == net::Transfer::Result::Ok;
    if (req_.upload && req_.proto != 'Z') endFile(good);  // XMODEM has no end-of-file frame of its own
    if (!good && note_.empty()) note_ = xfer_->error();
    if (!req_.upload) {
        ok_ = good;
        done_ = true;
        return false;
    }
    if (kept_) {
        size_t dot = saved_.rfind('.');
        std::string ext = dot == std::string::npos ? "" : saved_.substr(dot);
        for (char& c : ext) c = (char)std::toupper((unsigned char)c);
        if (ext == ".ZIP") {
            wantWorkspace_ = true;
            return true;
        }
    }
    finish();
    if (!ok_ && note_.empty()) note_ = "nothing was saved";
    return false;
}

}  // namespace axis::bbs
