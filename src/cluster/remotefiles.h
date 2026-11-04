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

private:
    bbs::FileLibrary&                                    lib_;
    std::map<int, std::unique_ptr<bbs::WriteFile>>       uploads_;
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

private:
    Ask ask_;
};

}  // namespace axis::cluster
