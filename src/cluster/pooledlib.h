// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// SD card clustering, the pool: on the main, one file library made of every SD card in the cluster
// (this board's own and each card follower's, reached over the link). An area lists every card's
// files as one list, a file kept on two cards showing once; a download reads from a card that has
// it (this board first), an upload goes to the card with the most room, and a sysop remove or rename
// reaches every copy. A card that is down simply adds nothing. Portable: the board supplies the
// cards (cluster.h cardBoards, a RemoteFileLibrary over requestTo for each).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "bbs/filelib.h"

namespace axis::cluster {

// One SD card the pool can use.
struct PoolCard {
    std::string       id;     // the board's id
    std::string       label;  // for the log and @CARDS: "this board", "node 2"
    bbs::FileLibrary* files = nullptr;
    uint64_t          freeBytes = 0;
    bool              alive = false;
};

class PooledFileLibrary : public bbs::FileLibrary {
public:
    using Cards = std::function<std::vector<PoolCard>()>;  // this board's card first
    explicit PooledFileLibrary(Cards cards) : cards_(std::move(cards)) {}

    std::vector<bbs::FileEntry>      list(const std::string& areaId) override;
    bool                             read(const std::string& areaId, const std::string& name, std::string& out, size_t max) override;
    std::unique_ptr<bbs::RandomFile> open(const std::string& areaId, const std::string& name) override;
    std::unique_ptr<bbs::WriteFile>  create(const std::string& areaId, const std::string& name) override;
    bool describe(const std::string& areaId, const std::string& name, const std::string& desc) override;
    bool remove(const std::string& areaId, const std::string& name) override;
    bool rename(const std::string& areaId, const std::string& from, const std::string& to) override;

    // An area's files across the given cards, each with the ids of the cards holding it (the copy
    // listed first: the largest; same size counts as the same file and the earlier card wins).
    struct Placed {
        bbs::FileEntry           entry;
        std::vector<std::string> holders;
    };
    static std::vector<Placed> locate(const std::string& areaId, const std::vector<PoolCard>& cards);
    std::vector<PoolCard>      cards() { return cards_ ? cards_() : std::vector<PoolCard>(); }

private:
    std::vector<PoolCard*> order(std::vector<PoolCard>& cards, const std::string& areaId, const std::string& name);
    void                   placed(const std::string& areaId, const std::string& name, const std::string& card);

    Cards cards_;
    // the last area listed: per file (by a hash of its name) which cards hold it, so a download
    // goes to the copy that was listed. Small on purpose: a few bytes a file.
    struct Known {
        uint32_t hash;
        uint32_t holders;  // bit i: the i-th card id in knownIds_
        uint8_t  first;    // the card whose copy was listed
    };
    std::string              knownArea_;
    std::vector<std::string> knownIds_;
    std::vector<Known>       known_;
    // the last few files created, renamed or removed: where each now lives (an upload's
    // description goes to its card, even after the upload processor rezips it under a temp name)
    struct Where {
        std::string area, name, card;
    };
    std::vector<Where> recent_;
};

}  // namespace axis::cluster
