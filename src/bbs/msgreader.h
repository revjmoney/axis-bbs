// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// [M]essages: the areas with their counts and how many are new to this caller, an area's newest
// headers, reading by number or the next new one, posting and replying (a body ends with /S).
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "bbs/config.h"
#include "bbs/msgbase.h"

namespace axis::bbs {

class MessageReader {
public:
    static constexpr int kPerPage = 15, kMaxLines = 99;
    // lastRead: the caller's newest-read number per area (kept with their account)
    MessageReader(const std::vector<FileArea>& areas, MessageStore& store, std::string& out, std::string handle,
                  std::map<std::string, int>& lastRead, std::function<std::string()> now)
        : areas_(areas), st_(store), out_(out), handle_(std::move(handle)), lastRead_(lastRead), now_(std::move(now)) {}
    void        setTagline(std::string t) { tagline_ = std::move(t); }  // signs each post
    void        begin();
    // the new scan: every area's new messages one after another ([N] moves on, across areas)
    void        beginScan();
    static int  countNew(const std::vector<FileArea>& areas, MessageStore& store, const std::map<std::string, int>& lastRead);
    bool        key(char c);  // false once the caller leaves for the main menu
    std::string where() const;

private:
    enum class St { Areas, List, ReadNum, Reading, To, Subject, Body };
    void showAreas();
    void openArea(size_t i);
    void showList();
    void listPrompt();
    void show(int num);
    void nextNew();
    void startPost(bool reply);
    void save();
    bool lineKey(char c, size_t max);  // true on Enter

    const std::vector<FileArea>& areas_;
    MessageStore&                st_;
    std::string                  tagline_;
    bool                         scanning_ = false;
    std::string&                 out_;
    std::string                  handle_;
    std::map<std::string, int>&  lastRead_;
    std::function<std::string()> now_;
    St                           s_ = St::Areas;
    size_t                       area_ = 0;
    std::vector<Message>         heads_;
    Message                      current_, draft_;
    std::string                  line_;
    int                          lines_ = 0;
};

}  // namespace axis::bbs
