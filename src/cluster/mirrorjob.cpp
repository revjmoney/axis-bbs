// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "cluster/mirrorjob.h"

#include <new>

namespace axis::cluster {

namespace {
bool usable(const PoolCard& c) { return c.alive && c.files; }
}

void MirrorJob::event(const std::string& e) {
    last_ = e;
    if (onEvent) onEvent(e);
}

bool MirrorJob::live(const std::string& id, std::string* label) {
    for (const PoolCard& c : pool_.cards())
        if (c.id == id && usable(c)) {
            if (label) *label = c.label;
            return true;
        }
    return false;
}

void MirrorJob::step(uint32_t now) {
    now_ = now;
    switch (stage_) {
        case Stage::Rest:
            if (started_ && now - restSince_ < (more_ ? 1000u : kRestMs)) return;
            started_ = true;
            more_ = false;
            passAreas_ = areas_ ? areas_() : std::vector<std::string>();
            areaAt_ = 0;
            todo_.clear();
            stage_ = Stage::Scan;
            return;
        case Stage::Scan:
            if (!todo_.empty()) startNext();
            else if (areaAt_ < passAreas_.size()) scanOne();
            else {  // the pass is over: rest, and give the memory back
                stage_ = Stage::Rest;
                restSince_ = now;
                std::vector<std::string>().swap(passAreas_);
                std::vector<Todo>().swap(todo_);
            }
            return;
        case Stage::Copy:
            copyPiece();
            return;
    }
}

// One mirrored area: the files only one live card holds go on the to-do list.
void MirrorJob::scanOne() {
    const std::string     area = passAreas_[areaAt_++];
    std::vector<PoolCard> cs = pool_.cards();
    int                   alive = 0;
    for (const PoolCard& c : cs) alive += usable(c) ? 1 : 0;
    if (alive < 2) {  // nowhere to put a second copy: skip the rest of this pass
        areaAt_ = passAreas_.size();
        return;
    }
    for (PooledFileLibrary::Placed& p : PooledFileLibrary::locate(area, cs)) {
        if (p.holders.size() != 1) continue;
        if (todo_.size() >= kTodoMax) {  // the rest next pass, which comes sooner
            more_ = true;
            break;
        }
        todo_.push_back({area, p.entry.name, p.entry.desc, p.holders[0]});
    }
}

// The next file on the list: open it on its card, pick the live card with the most room for it.
void MirrorJob::startNext() {
    cur_ = std::move(todo_.front());
    todo_.erase(todo_.begin());
    std::vector<PoolCard> cs = pool_.cards();
    PoolCard *            from = nullptr, *to = nullptr;
    for (PoolCard& c : cs)
        if (usable(c) && c.id == cur_.from) from = &c;
    if (!from || !(src_ = from->files->open(cur_.area, cur_.name))) return;  // gone since the scan
    size_ = src_->size();
    for (PoolCard& c : cs)
        if (usable(c) && c.id != cur_.from && c.freeBytes >= size_ + kSpare && (!to || c.freeBytes > to->freeBytes)) to = &c;
    if (!to) {  // no card with room for it: wait quietly for one
        src_.reset();
        return;
    }
    fromLabel_ = from->label;
    to_ = to->id;
    toLabel_ = to->label;
    buf_.reset(new (std::nothrow) uint8_t[kChunk]);
    if (buf_) dst_ = to->files->create(cur_.area, cur_.name);
    if (!dst_) {
        src_.reset();
        buf_.reset();
        failed_++;
        event("mirror: " + toLabel_ + " would not take " + cur_.name);
        return;
    }
    off_ = 0;
    stage_ = Stage::Copy;
}

void MirrorJob::copyPiece() {
    if (!live(cur_.from) || !live(to_)) return stop(false, "a board left");
    uint32_t n = size_ - off_ < kChunk ? size_ - off_ : (uint32_t)kChunk;
    if (n && !src_->readAt(off_, buf_.get(), n)) return stop(false, "read failed");
    if (n && !dst_->write(buf_.get(), n)) return stop(false, "write failed");
    off_ += n;
    if (off_ < size_) return;
    // whole: keep it only if the file is still there as it was (an upload being rezipped changes it)
    src_.reset();
    std::vector<PoolCard> cs = pool_.cards();
    for (PoolCard& c : cs)
        if (usable(c) && c.id == cur_.from) src_ = c.files->open(cur_.area, cur_.name);
    if (!src_ || src_->size() != size_) return stop(false, "the file changed");
    stop(true, "");
}

void MirrorJob::stop(bool keep, const std::string& why) {
    bool ok = dst_ && dst_->finish(keep) && keep;
    dst_.reset();
    src_.reset();
    buf_.reset();
    stage_ = Stage::Scan;
    if (!ok) {
        failed_++;
        event("mirror: copy of " + cur_.name + " to " + toLabel_ + " stopped (" + (why.empty() ? "not kept" : why) + "), trying again later");
        return;
    }
    copied_++;
    if (!cur_.desc.empty())  // the copy keeps the file's description in its card's FILES.BBS
        for (PoolCard& c : pool_.cards())
            if (usable(c) && c.id == to_) c.files->describe(cur_.area, cur_.name, cur_.desc);
    event("mirror: copied " + cur_.name + " to " + toLabel_);
}

std::string MirrorJob::state() const {
    std::string s;
    if (stage_ == Stage::Copy)
        s = "copying " + cur_.name + " (" + std::to_string(off_) + " of " + std::to_string(size_) + " bytes) from " + fromLabel_ + " to " +
            toLabel_;
    else if (stage_ == Stage::Scan)
        s = "checking the mirrored areas (" + std::to_string(areaAt_) + " of " + std::to_string(passAreas_.size()) + ")";
    else if (!started_)
        s = "waiting for its first pass";
    else {
        uint32_t wait = more_ ? 1000u : kRestMs, gone = now_ - restSince_;
        s = "resting, next pass in " + std::to_string(gone < wait ? (wait - gone) / 1000 : 0) + " s";
    }
    s += "; copied " + std::to_string(copied_) + ", failed " + std::to_string(failed_);
    if (!last_.empty()) s += "; last: " + last_;
    return s;
}

}  // namespace axis::cluster
