// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// File areas over the cluster link. The main owns the SD card; a follower's FileLibrary asks the
// main for everything (list, read, downloads a piece at a time, uploads, FILES.BBS, sysop remove
// and rename). A request is an operation letter and fields separated by 0x1F; an upload's data is
// the last field, taken whole. A reply is '1' (ok) or '0', then its data, raw. Pieces stay small
// (kChunk) so a frame never needs much of the board's fragmented heap.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "bbs/features.h"
#include "bbs/filelib.h"

namespace axis::cluster {

// The main's side: answers one request with one reply.
class FileServer {
public:
    static constexpr size_t kChunk = 2048;
    static constexpr size_t kListMax = 7 * 1024;  // an area listing stops here: a link frame is 8 KB at most

    explicit FileServer(bbs::FileLibrary& lib) : lib_(lib) {}
    std::string handle(const std::string& request);
    size_t      openUploads() const { return uploads_.size(); }
    void        dropUploads() { uploads_.clear(); }  // the follower went away: its uploads are thrown out

#if AXIS_NETS
    // Net Files for a follower's caller (main.cpp, with the nets; unset, these ops answer "0"): G a page, b begin or poll
    // a fetch, t take bytes, e end it. owner is this server (one per follower board), so a follower ends only its own fetch.
    static std::string (*netOp)(const void* owner, char op, const std::vector<std::string>& fields);
    // A follower's upload landed: kept (F) and then described (D, which carries the uploader's handle, "" from an older
    // follower). Unset: nothing.
    static void (*uploaded)(const std::string& area, const std::string& name, const std::string& desc, const std::string& who);
#endif

private:
    struct Upload {
        std::unique_ptr<bbs::WriteFile> w;
#if AXIS_NETS
        std::string at;  // "area<1F>name"
#endif
    };
    bbs::FileLibrary&                                    lib_;
    std::map<int, Upload>                                uploads_;
#if AXIS_NETS
    std::vector<std::string>                             kept_;  // uploads kept and not described yet ("area<1F>name")
#endif
    int                                                  nextUpload_ = 1;
    std::string                                          openArea_, openName_;  // the last file read from
    std::unique_ptr<bbs::RandomFile>                     open_;
};

// The follower's side. ask(request, reply) sends one request over the link and waits for the
// reply; false when there is no main to ask.
class RemoteFileLibrary : public bbs::FileLibrary {
public:
    using Ask = std::function<bool(const std::string& request, std::string& reply)>;
    explicit RemoteFileLibrary(Ask ask) : ask_(std::move(ask)) {}

    std::vector<bbs::FileEntry>      list(const std::string& areaId) override;
    bool                             read(const std::string& areaId, const std::string& name, std::string& out, size_t max) override;
    std::unique_ptr<bbs::RandomFile> open(const std::string& areaId, const std::string& name) override;
    std::unique_ptr<bbs::WriteFile>  create(const std::string& areaId, const std::string& name) override;
    bool describe(const std::string& areaId, const std::string& name, const std::string& desc) override;
    bool remove(const std::string& areaId, const std::string& name) override;
    bool rename(const std::string& areaId, const std::string& from, const std::string& to) override;

    bool call(std::initializer_list<std::string> fields, std::string& data);  // true if the main said ok
    bool raw(std::initializer_list<std::string> fields, std::string& reply);   // the whole reply; false: no main to ask
    bool raw(const std::string& request, std::string& reply);

#if AXIS_NETS
    std::string (*who)() = nullptr;  // the caller whose upload is described (sent with D, so the main lists it as theirs)

    // Net Files through the main. netPage: one page (rows as the main's own view, a row that can't be reached already
    // marked in its description; ids and home names alongside); the next skip, -1 at the end or when the main says no,
    // -2 when it is short of memory. netFetch: begin a fetch, or (the same id again, no handle) ask about the one begun:
    // the reply's letter, the rest in `rest` ('2' connecting; '1' ready, "size<1F>home"; '3' the main's own file,
    // "area<1F>name"; '0' refused or failed, the word), or 0 when the main could not be asked.
    int  netPage(int skip, int max, std::vector<bbs::FileEntry>& out, std::vector<std::string>& ids, std::vector<std::string>& homes);
    char netFetch(const std::string& id, const std::string& handle, int level, std::string& rest);
#endif

private:
    Ask ask_;
};

#if AXIS_NETS
// A Net Files download on a follower: the main's fetch, 2 KB at a time ("t"). ready() is the sender's ReadyAt: false while
// the main has not got the bytes yet ("2", asked again no sooner than kWaitMs later when a clock is given), so the sender
// waits; "0<word>" (or a lost main) ends it: ready true, reads fail, word() says why. The host ends the fetch ("e").
class RemoteFetchFile : public bbs::RandomFile {
public:
    static constexpr uint32_t kWaitMs = 50;
    RemoteFetchFile(RemoteFileLibrary& lib, uint32_t size, uint32_t (*clock)() = nullptr) : lib_(lib), size_(size), clock_(clock) {}
    uint32_t           size() const override { return size_; }
    bool               readAt(uint32_t off, uint8_t* buf, size_t n) override;
    bool               ready(uint32_t off, size_t n);
    const std::string& word() const { return word_; }  // "" while the fetch goes on

private:
    bool has(uint32_t off, size_t n) const { return off >= aheadAt_ && (uint64_t)off + n <= (uint64_t)aheadAt_ + ahead_.size(); }
    RemoteFileLibrary& lib_;
    uint32_t           size_;
    uint32_t (*clock_)();
    std::string ahead_, word_;  // the last piece taken, at aheadAt_
    uint32_t    aheadAt_ = 0, waitMs_ = 0;
    bool        waiting_ = false;
};
#endif

}  // namespace axis::cluster
