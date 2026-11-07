// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Axis Net: a directory of other Axis Micro boards, and the short greeting they
// exchange. The menu is a lightbar like the file and message lists. The host owns
// the socket: this class only decides what to say and which board to call.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "bbs/lightbar.h"
#include "bbs/listscreen.h"

namespace axis::bbs {

struct AxisCard {
    std::string name, host, location, sysop, blurb;
    int         port = 23;
    int         online = -1;  // -1: the board has not said
    uint32_t    seen = 0;     // unix time it last spoke, 0 if we only know the address
};

std::string axisLine(const AxisCard& c);
bool        axisParse(const std::string& line, AxisCard& c);
void        axisMerge(std::vector<std::string>& lines, const AxisCard& c, size_t cap = 24);
bool        axisHostPort(const std::string& text, std::string& host, int& port);

// What we send a peer, and what we keep from their reply. Lines are axisLine cards.
// Notes on the wire are "from|to|text". ourHost is not stored again as a peer.
std::string axisGreet(const AxisCard& self, const std::vector<std::string>& known, const std::vector<std::string>& notes);
struct AxisHeard {
    std::vector<std::string> notes;  // "from|to|text", theirs
    bool                     ok = false;
};
AxisHeard axisTake(const std::string& reply, std::vector<std::string>& lines, const std::string& ourHost);

// The caller's Axis Net screen. begin() draws it. key() returns false when they leave.
// Enter on a remote board asks the host to call it. A, N, R and Q are the other keys.
class AxisNetMenu {
public:
    // self() is this board's own card, built by the host (name, public address, who is on).
    AxisNetMenu(std::string& out, std::function<AxisCard()> self, const std::string& handle,
                std::function<std::vector<std::string>(const std::string&)> get,
                std::function<void(const std::string&, const std::string&)> add, std::function<uint32_t()> now);
    void begin();
    bool key(char c);
    bool tick(uint32_t nowMs, uint32_t lastInput);  // false: Esc left the list
    bool refresh();                                 // redraw the list if they are still on it
    bool wantsCall() const { return calling_; }
    const std::string& callHost() const { return callHost_; }
    int                callPort() const { return callPort_; }
    void               callDone(bool ok, const char* why = nullptr);
    bool               takeProbe();  // true once after R, or after a note that should go out
    std::string        where() const;

private:
    enum class St { List, Add, Note };
    void show();
    void status();
    void setStatus(const std::string& line);
    bool lineKey(char c, size_t max);
    std::vector<AxisCard> load() const;
    std::string&                                                 out_;
    std::function<AxisCard()>                                    self_;
    const std::string&                                           handle_;
    std::function<std::vector<std::string>(const std::string&)>  get_;
    std::function<void(const std::string&, const std::string&)>  add_;
    std::function<uint32_t()>                                    now_;
    std::vector<AxisCard>                                        cards_;
    ScrollList                                                   list_{3, kListRow, 74, kListRows, 111, 15};
    St                                                           st_ = St::List;
    std::string                                                  line_, callHost_;
    int                                                          callPort_ = 23;
    bool                                                         calling_ = false, probe_ = false;
};

}  // namespace axis::bbs
