// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// SD card clustering, the mirror: on the main, files in a mirrored area ("areaN=id|name|level|m")
// that only one live card holds are copied to a second card with room, in the background. The main
// loop calls step() every turn and each call does one small thing: list one area, or copy one piece
// (at most kChunk bytes) of one file. A copy is written through the target card's create(), so it
// only shows up once it is whole; a copy cut short (a board left, a read or write failed) is thrown
// away and tried again on a later pass. Portable: the cards come from the pool (pooledlib.h).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cluster/pooledlib.h"

namespace axis::cluster {

class MirrorJob {
public:
    static constexpr size_t   kChunk = 2048;            // one piece per step
    static constexpr uint64_t kSpare = 1024 * 1024;     // a target keeps at least this much free
    static constexpr uint32_t kRestMs = 30000;          // between passes over the mirrored areas
    static constexpr size_t   kTodoMax = 8;             // files remembered per pass (the rest wait for the next)

    using Areas = std::function<std::vector<std::string>()>;  // the mirrored areas' ids
    MirrorJob(PooledFileLibrary& pool, Areas areas) : pool_(pool), areas_(std::move(areas)) {}

    void        step(uint32_t nowMs);
    std::string state() const;  // for @MIRROR: what it is doing, and what it has done
    bool        copying() const { return dst_ != nullptr; }
    uint32_t    copied() const { return copied_; }
    uint32_t    failed() const { return failed_; }

    std::function<void(const std::string&)> onEvent;  // "mirror: copied X to node 2"

private:
    enum class Stage { Rest, Scan, Copy };
    struct Todo {
        std::string area, name, desc, from;  // from: the one card holding it
    };
    void scanOne();
    void startNext();
    void copyPiece();
    void stop(bool keep, const std::string& why);
    bool live(const std::string& id, std::string* label = nullptr);
    void event(const std::string& e);

    PooledFileLibrary&               pool_;
    Areas                            areas_;
    Stage                            stage_ = Stage::Rest;
    bool                             started_ = false, more_ = false;
    uint32_t                         now_ = 0, restSince_ = 0;
    std::vector<std::string>         passAreas_;
    size_t                           areaAt_ = 0;
    std::vector<Todo>                todo_;
    Todo                             cur_;
    std::string                      fromLabel_, to_, toLabel_;  // the card copied from, and the target
    std::unique_ptr<bbs::RandomFile> src_;
    std::unique_ptr<bbs::WriteFile>  dst_;
    std::unique_ptr<uint8_t[]>       buf_;  // kChunk, only while copying
    uint32_t                         off_ = 0, size_ = 0;
    uint32_t                         copied_ = 0, failed_ = 0;
    std::string                      last_;  // the last thing it logged
};

}  // namespace axis::cluster
