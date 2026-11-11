// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/diag.h"

#include <cstdio>

namespace axis::bbs {

const char* doorDiagToken(DoorDiag d) {
    switch (d) {
    case DoorDiag::Running: return "running";
    case DoorDiag::Quit: return "quit";
    case DoorDiag::OutOfMemory: return "oom";
    case DoorDiag::Hangup: return "hangup";
    case DoorDiag::TimeLimit: return "time";
    case DoorDiag::LoadError: return "load";
    case DoorDiag::None: break;
    }
    return "none";
}

const char* resetToken(int resetCode) {
    // The numbers are esp_reset_reason_t in ESP-IDF 4.4. Kept here so the PC tests can name them
    // without the IDF headers.
    switch (resetCode) {
    case 1: return "poweron";
    case 2: return "ext";
    case 3: return "sw";
    case 4: return "panic";
    case 5: return "intwdt";
    case 6: return "taskwdt";
    case 7: return "wdt";
    case 8: return "deepsleep";
    case 9: return "brownout";
    case 10: return "sdio";
    default: return "unknown";
    }
}

std::string diagFields(const DiagSample& s) {
    std::string tok = s.winDoing.substr(0, 24);
    for (char& c : tok)
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') c = '_';
    if (tok.empty()) tok = "none";
    char b[320];
    std::snprintf(b, sizeof b,
                  " min=%u stack=%u door_peak=%u door=%s door_stack=%u reset=%s wmin=%u wlarge=%u wdoing=%s stk_web=%u stk_mdns=%u "
                  "stk_timer=%u outmax=%u artmax=%u",
                  s.minHeap, s.stackWords, s.doorPeakKb, doorDiagToken(s.door), s.doorStackWords, resetToken(s.resetCode), s.winFree,
                  s.winLargest, tok.c_str(), s.stkWeb, s.stkMdns, s.stkTimer, s.outMax, s.artMax);
    return b;
}

}  // namespace axis::bbs
