// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/uploadproc.h"

#include <cctype>

#include "net/crc.h"

namespace axis::bbs {

namespace {
std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
void put16(std::string& s, uint16_t v) {
    s += (char)(v & 0xFF);
    s += (char)(v >> 8);
}
void put32(std::string& s, uint32_t v) {
    put16(s, (uint16_t)v);
    put16(s, (uint16_t)(v >> 16));
}
uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

std::string infoText(const UploadInfo& i) {
    std::string t;
    t += " \xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\xdc\r\n";
    t += " \xdb  " + i.bbsName + "\r\n";
    t += " \xdb\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\xdf\r\n";
    t += " \xdb  \xdb \xdf\xdb  \xdb   \xdf  \xdb    \xdb  \xdf\xdb   \xdb  \xdb \xdf   \xdb\r\n";
    t += "    \xdf    \xdf             \xdf          \xdf\r\n\r\n";
    t += "   This file came from " + i.bbsName + ".\r\n";
    t += "   telnet " + i.address + "   \xfa   " + i.location + "\r\n";
    t += "   running " + i.software + " by Rev. J. Money\r\n";
    t += "   rotatocam.com   \xfa   jmscnc.com\r\n\r\n";
    t += "   Every file in this archive passed its CRC check when it was uploaded.\r\n";
    return t;
}
}  // namespace

UploadProcessor::UploadProcessor(FileLibrary& lib, std::string area, std::string name, UploadInfo info, uint8_t* workspace,
                                 size_t workspaceSize)
    : lib_(lib), area_(std::move(area)), name_(std::move(name)), info_(std::move(info)), ws_(workspace), wsSize_(workspaceSize) {
    temp_ = name_ + ".NEW";
}

double UploadProcessor::progress() const {
    double n = keep_.empty() ? 1 : (double)keep_.size();
    switch (stage_) {
        case Stage::Open: return 0;
        case Stage::Check: return 0.05 + 0.5 * at_ / n;
        case Stage::Write: return 0.55 + 0.35 * at_ / n;
        case Stage::Info: return 0.92;
        case Stage::Finish: return 0.96;
        case Stage::Done: return 1;
    }
    return 1;
}

std::string UploadProcessor::current() const {
    if (!zip_ || at_ >= keep_.size()) return stage_ == Stage::Info ? kInfoName : "";
    return zip_->entries()[keep_[at_]].name;
}

bool UploadProcessor::reject(const std::string& why) {
    if (out_) out_->finish(false);
    out_.reset();
    zip_.reset();
    in_.reset();
    lib_.remove(area_, temp_);
    lib_.remove(area_, name_);
    note_ = why;
    result_ = Result::Rejected;
    stage_ = Stage::Done;
    return false;
}

bool UploadProcessor::writeEntry(const std::string& name, uint16_t method, uint16_t flags, uint16_t time, uint16_t date,
                                 uint32_t crc, uint32_t csize, uint32_t usize) {
    std::string h;
    put32(h, 0x04034b50);
    put16(h, 20);  // version needed: 2.0, plain deflate
    put16(h, flags);
    put16(h, method);
    put16(h, time);
    put16(h, date);
    put32(h, crc);
    put32(h, csize);
    put32(h, usize);
    put16(h, (uint16_t)name.size());
    put16(h, 0);
    h += name;
    if (!out_->write((const uint8_t*)h.data(), h.size())) return false;
    written_.push_back({name, method, flags, time, date, crc, csize, usize, outPos_});
    outPos_ += (uint32_t)h.size();
    return true;
}

bool UploadProcessor::step() {
    if (stage_ == Stage::Done) return false;
    switch (stage_) {
        case Stage::Open: {
            size_t dot = name_.rfind('.');
            if (dot == std::string::npos || upper(name_.substr(dot)) != ".ZIP") {
                result_ = Result::Kept;
                note_ = "not a ZIP: kept as uploaded";
                stage_ = Stage::Done;
                return false;
            }
            in_ = lib_.open(area_, name_);
            if (!in_) return reject("the upload couldn't be read back");
            RandomFile* f = in_.get();
            zip_.reset(new ZipReader([f](uint32_t off, uint8_t* buf, size_t n) { return f->readAt(off, buf, n); }, f->size()));
            if (!zip_->open() || zip_->entries().empty()) return reject("it isn't a readable ZIP (damaged or not a ZIP)");
            for (size_t i = 0; i < zip_->entries().size(); i++) {
                const ZipEntry& e = zip_->entries()[i];
                if (e.method != 0 && e.method != 8)
                    return reject("packed with an old PKZIP method (" + e.name + "); upload a standard ZIP");
                if (e.flags & 1) return reject("it's password protected");
                std::string base = e.name.substr(e.name.find_last_of('/') + 1);
                if (upper(base) != kInfoName) keep_.push_back(i);
            }
            stage_ = Stage::Check;
            at_ = 0;
            return true;
        }
        case Stage::Check: {
            if (at_ >= keep_.size()) {
                out_ = lib_.create(area_, temp_);
                if (!out_) return reject("the area can't take the rezipped file");
                stage_ = Stage::Write;
                at_ = 0;
                return true;
            }
            const ZipEntry& e = zip_->entries()[keep_[at_]];
            uint32_t        crc = 0xFFFFFFFFu, size = 0;
            auto res = zip_->extract(e, ws_, wsSize_, [&](const uint8_t* p, size_t n) {
                crc = net::crc32Update(crc, p, n);
                size += (uint32_t)n;
                return true;
            }, state_);
            if (res == ZipReader::Result::NoMemory) return reject("the board was short of memory to check it: please try again");
            if (res != ZipReader::Result::Ok || ~crc != e.crc || size != e.usize)
                return reject("it's damaged: " + e.name + " failed its CRC check");
            at_++;
            return true;
        }
        case Stage::Write: {
            if (at_ >= keep_.size()) {
                stage_ = Stage::Info;
                return true;
            }
            const ZipEntry& e = zip_->entries()[keep_[at_]];
            uint8_t         lh[30];
            if (!in_->readAt(e.local, lh, 30)) return reject("read error while rezipping");
            uint32_t data = e.local + 30 + get16(lh + 26) + get16(lh + 28);
            // sizes and CRC go in the header now, so the "data descriptor follows" flag goes
            if (!writeEntry(e.name, e.method, (uint16_t)(e.flags & ~0x0008), e.time, e.date, e.crc, e.csize, e.usize))
                return reject("write error while rezipping");
            uint8_t buf[512];
            for (uint32_t done = 0; done < e.csize;) {
                uint32_t n = e.csize - done < sizeof buf ? e.csize - done : (uint32_t)sizeof buf;
                if (!in_->readAt(data + done, buf, n) || !out_->write(buf, n)) return reject("write error while rezipping");
                done += n;
            }
            outPos_ += e.csize;
            at_++;
            return true;
        }
        case Stage::Info: {
            std::string text = infoText(info_);
            uint32_t    crc = net::crc32((const uint8_t*)text.data(), text.size());
            uint16_t    time = 0, date = (uint16_t)((2026 - 1980) << 9 | 11 << 5 | 3);
            if (!writeEntry(kInfoName, 0, 0, time, date, crc, (uint32_t)text.size(), (uint32_t)text.size()) ||
                !out_->write((const uint8_t*)text.data(), text.size()))
                return reject("write error while adding " + std::string(kInfoName));
            outPos_ += (uint32_t)text.size();
            stage_ = Stage::Finish;
            return true;
        }
        case Stage::Finish: {
            std::string cd;
            for (const Written& w : written_) {
                put32(cd, 0x02014b50);
                put16(cd, 20);  // made by: MS-DOS, 2.0
                put16(cd, 20);
                put16(cd, w.flags);
                put16(cd, w.method);
                put16(cd, w.time);
                put16(cd, w.date);
                put32(cd, w.crc);
                put32(cd, w.csize);
                put32(cd, w.usize);
                put16(cd, (uint16_t)w.name.size());
                put16(cd, 0);
                put16(cd, 0);
                put16(cd, 0);
                put16(cd, 0);
                put32(cd, 0);
                put32(cd, w.offset);
                cd += w.name;
            }
            std::string end;
            put32(end, 0x06054b50);
            put16(end, 0);
            put16(end, 0);
            put16(end, (uint16_t)written_.size());
            put16(end, (uint16_t)written_.size());
            put32(end, (uint32_t)cd.size());
            put32(end, outPos_);
            put16(end, 0);
            cd += end;
            if (!out_->write((const uint8_t*)cd.data(), cd.size()) || !out_->finish(true)) return reject("write error finishing the ZIP");
            out_.reset();
            zip_.reset();
            in_.reset();
            if (!lib_.remove(area_, name_) || !lib_.rename(area_, temp_, name_)) {
                note_ = "rezipped, but the new copy couldn't replace the upload";
                result_ = Result::Rejected;
            } else {
                result_ = Result::Rezipped;
                note_ = "checked " + std::to_string(written_.size() - 1) + " files, added " + kInfoName;
            }
            stage_ = Stage::Done;
            return false;
        }
        case Stage::Done:
            return false;
    }
    return false;
}

}  // namespace axis::bbs
