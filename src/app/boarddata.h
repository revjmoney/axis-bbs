// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's small shared data: the oneliner wall, the last callers and the lists (rumors, the BBS list, votes, ballots,
// the Axis Net cards and notes), in the flash folder (host.flashRoot()) with the files, formats and limits the board has
// always used. In a cluster the main keeps them: a follower reads and posts over the link (`follower` and `remote`).
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "app/host.h"

namespace axis::app {

// A whole file in the flash folder ("/stats.txt" is <flashRoot>/stats.txt), empty when it is not there.
std::string flashRead(Host& host, const char* path);
// Written to /save.tmp first, then renamed over the old one, so a power cut never leaves half a file.
bool        flashWrite(Host& host, const char* path, const std::string& text);
// Whether a file is in the flash folder.
bool        flashExists(Host& host, const char* path);

class BoardData {
public:
    explicit BoardData(Host& h) : host_(h) {}

    // A cluster follower now: the main keeps the data (empty: never, as on the PC).
    std::function<bool()> follower;
    // Asks the main (the follower's link); false when it did not answer. Empty on the PC.
    std::function<bool(const std::string& req, std::string& reply)> remote;

    // At boot, once the flash folder is there: the oneliners and the last callers from their files.
    void load();

    // The oneliner wall: the newest 20, /oneliners.txt.
    const std::vector<std::string>& oneLiners() const { return oneLiners_; }
    void                            addOneLiner(const std::string& line);
    // The last callers: the newest 20 logins ("handle|node|time|location"), /callers.txt.
    const std::vector<std::string>& lastCallers() const { return lastCallers_; }
    void                            noteCaller(const std::string& line);

    // The lists, /list_<name>.txt.
    static bool              listName(const std::string& n);
    static size_t            listCap(const std::string& n);
    std::vector<std::string> listLoad(const std::string& n);  // "rumor": one random line of the rumors
    void                     listSave(const std::string& n, const std::vector<std::string>& v);
    void                     listAdd(const std::string& n, const std::string& line);
    void                     forgetCounts();  // a list file may have changed behind our back: count it again when next needed

    // The same, on a follower from the main (on the main or the PC, this board's own).
    std::vector<std::string> sharedOneLiners();
    void                     sharedAddOneLiner(const std::string& line);
    std::vector<std::string> sharedLastCallers();
    void                     sharedNoteCaller(const std::string& line);
    std::vector<std::string> sharedListLoad(const std::string& n);
    std::vector<std::string> sharedList(const std::string& n);  // "callers": the last callers
    void                     sharedListAdd(const std::string& n, const std::string& line);

private:
    bool                     isFollower() { return follower && follower(); }
    std::vector<std::string> remoteLines(const std::string& req);
    void                     remoteSend(const std::string& req);
    std::string              listPath(const std::string& n);
    size_t                   listCountOf(const std::string& n);
    size_t                   listSkip(const std::string& n);
    std::vector<std::string> listOne(const std::string& n);
    bool                     listAppendLocal(const std::string& n, const std::string& line, size_t cap);

    Host&                    host_;
    std::vector<std::string> oneLiners_;
    std::vector<std::string> lastCallers_;
    // Lines counted per list the first time it is touched, then kept in step by listAppendLocal. listSave forgets it.
    int32_t listCount_[4] = {-1, -1, -1, -1};  // rumors, bbslist, votes, ballots; -1 = not counted yet
};

}  // namespace axis::app
