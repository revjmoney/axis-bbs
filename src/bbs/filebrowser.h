// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The caller's Files menu: pick an area, page through its files, view text files. The Node hands
// it every key while the caller is in Files and returns to its own menu when it is done.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "bbs/config.h"
#include "bbs/filelib.h"
#include "bbs/transferjob.h"

namespace axis::bbs {

class FileBrowser {
    std::function<std::string(uint32_t)> gate_;
    uint32_t                             since_ = 0;
    void                                 showNewFiles();

public:
    static constexpr int kPerPage = 15, kViewLines = 20;
    static constexpr size_t kMaxView = 64 * 1024;

    FileBrowser(const std::vector<FileArea>& areas, FileLibrary& lib, std::string& out)
        : areas_(areas), lib_(lib), out_(out) {}
    // downloads and viewing: "" = allowed, else why not (a new caller uploads something first)
    // (size: the file's bytes, 0 for viewing)
    void setDownloadGate(std::function<std::string(uint32_t size)> gate) { gate_ = std::move(gate); }
    void setNewSince(uint32_t t) { since_ = t; }  // [N] at the area list: files newer than this (the last call)
    void begin();       // show the area list
    std::string where() const;  // "area list" or the open area's name, for the sysop's activity screen
    bool key(char c);   // false once the caller leaves Files
    // a download or upload the caller asked for: the host runs it, then calls transferDone
    bool                   wantsTransfer() const { return st_ == St::Transfer; }
    const TransferRequest& transfer() const { return req_; }
    void transferDone(bool ok, uint32_t bytes, uint32_t ms, const std::string& note, uint32_t size);

private:
    enum class St { Areas, List, ViewName, Viewing, DownName, UpDesc, Proto, UpName, Transfer, InfoName };
    void showAreas();
    void openArea(size_t i);
    void showPage();
    void listPrompt();
    void view(const std::string& name);
    void info(const std::string& name);  // [I]: everything about a file, a ZIP's whole listing
    void showMoreText();
    void askProtocol();
    void startTransfer();
    bool lineKey(char c, size_t max);  // true when Enter ends the line (in line_)

    const std::vector<FileArea>& areas_;
    FileLibrary&                 lib_;
    std::string&                 out_;
    St                           st_ = St::Areas;
    std::string                  line_;
    size_t                       area_ = 0;
    int                          page_ = 0;
    std::vector<FileEntry>       files_;
    std::vector<std::string>     text_;  // the file being viewed, as lines
    size_t                       shown_ = 0;
    TransferRequest              req_;
};

}  // namespace axis::bbs
