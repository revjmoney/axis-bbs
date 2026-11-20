// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The waiting-for-caller screen: Renegade-style ANSI art with pipe codes (see docs/WFC-CODES.md) filled in with live values,
// plus where the hotkey letters and node lines landed so the CYD can make them tappable.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bbs/ansiterm.h"
#include "bbs/pipecodes.h"
#include "net/telnet.h"

namespace axis::bbs {

struct WfcNode {
    int         number = 0;
    std::string handle, activity;  // handle "" = waiting
    std::string link;              // "RADIO SF7 -81" for a caller on the LFD3K-SM modem (shown after the activity)
    std::string platform;          // "pc" or "esp": what the node runs on (nodePlatform), "" when not known
    bool        operator==(const WfcNode& o) const {
        return number == o.number && handle == o.handle && activity == o.activity && link == o.link && platform == o.platform;
    }
};

// What a node's board runs on, from its cluster id: the Linux program's id is a locally administered address (made from
// the machine id), which an ESP32's factory MAC never is. "pc" or "esp"; "" for an id that isn't a MAC.
std::string nodePlatform(const std::string& boardId);
std::string nodeLine(const WfcNode& n);  // "3 pc  waiting", "1 esp Rev  Messages": one row of the node list

// The newest LFD3K-SM link report on this board (the |R? codes), as the screen shows it; all "" before the first one.
// "The same" means the same caller, modem and tier: the numbers (label with RSSI, bps, SNR, RSSI, retrains, LZ) move at
// every report and must not redraw the WFC each time; they refresh with the next redraw (the minute tick at the latest).
struct WfcRadio {
    std::string label, who, modem, tier, bps, snr, rssi, retrains, lz;
    bool        operator==(const WfcRadio& o) const { return who == o.who && modem == o.modem && tier == o.tier; }
};
// who: the caller's handle (or "" for the modem's own caller name)
WfcRadio wfcRadio(const net::LinkInfo& l, const std::string& who);

struct WfcSnapshot {
    std::string              bbsName, sysop, lastCaller, ip, time, date, page;
    std::vector<WfcNode>     nodes;   // cluster-wide, by number
    std::vector<std::string> events;  // newest first
    uint32_t                 callsToday = 0, postsToday = 0, uploadsToday = 0, sdFreeKb = 0, ramFreeKb = 0, uptimeS = 0;
    uint32_t                 totalCalls = 0;  // every call ever on this board (|TC)
    bool                     main = true;  // "main board" / "follower board"
    WfcRadio                 radio;        // the newest radio link (|RL |RW |RO |RT |RB |RS |RR |RN |RZ)
    std::string              notice;       // drawn over the bottom row whatever the art (the SD card notice); "" = none
    // axispc only: a newer pc release blinks in the VN spot. Empty on the firmware, so the screen is unchanged there.
    // versionNotes is the sysop's release text (V, or System / Update). It is not drawn, so it is not part of "the same screen".
    std::string              versionBlink;
    int                      versionPhase = 0;  // 0: the version, 1: versionBlink
    std::string              versionNotes;
    // "the same screen": the uptime by the minute and free RAM in 8 KB steps (time is HH:MM), so an idle WFC is redrawn
    // once a minute, not at every 5 s check while the heap wobbles. The blink phase is part of the screen, so it does redraw.
    bool operator==(const WfcSnapshot& o) const {
        return bbsName == o.bbsName && sysop == o.sysop && lastCaller == o.lastCaller && ip == o.ip && time == o.time && date == o.date &&
               page == o.page && nodes == o.nodes && events == o.events && callsToday == o.callsToday && postsToday == o.postsToday &&
               uploadsToday == o.uploadsToday && totalCalls == o.totalCalls && sdFreeKb == o.sdFreeKb && ramFreeKb / 8 == o.ramFreeKb / 8 && uptimeS / 60 == o.uptimeS / 60 &&
               main == o.main && radio == o.radio && notice == o.notice && versionBlink == o.versionBlink && versionPhase == o.versionPhase;
    }
};

// How soon to draw the waiting screen again. 5 s, or half a second while a newer axispc release is blinking in the version spot.
inline uint32_t wfcRefreshMs(const WfcSnapshot& shown) { return shown.versionBlink.empty() ? 5000u : 500u; }

// the |XX / @XX###@ value function for expandArt (it reads `s` in place: `s` must outlive the function it returns)
ArtValue wfcValues(const WfcSnapshot& s);

struct WfcHotspot {
    int  row, col;
    char key;   // S U # B F L E A ! space; 'N' for a node line
    int  node;      // > 0 for a node line
    int  span = 1;  // cells from col that hit it: a key's label ("[S] settings"), a node line's field
};

// Expands WFC art into `term`, recording where the |K? and |N? codes landed.
std::vector<WfcHotspot> drawWfc(std::string_view art, const WfcSnapshot& s, AnsiTerm& term);

// When `s` has an update blink, the cells showing the version (or the blink text) become a 'V' tap: the release notes.
void wfcMarkVersion(const WfcSnapshot& s, const AnsiTerm& term, std::vector<WfcHotspot>& spots);

// A theme's WFC art made safe to draw or send: cut at kWfcArtCap and at a SAUCE/EOF mark, no escape left open at the end, bare LF
// made CR LF.
constexpr size_t kWfcArtCap = 12288;  // a theme's WFC.ANS past this is cut, as the board's theme loader does
std::string      cleanWfcArt(std::string a);

// The key tapped at (row, col), or 0 for none. A key hits on its cell, across its label (up to 14 cells, until two spaces, a box
// line or the next hotspot) and on the cell to its left; a node line hits across its field (sets *node to its number, else 0).
// Keys come first.
char wfcKeyAt(const std::vector<WfcHotspot>& spots, int row, int col, int* node);

}  // namespace axis::bbs
