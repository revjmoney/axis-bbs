// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's outbound HTTPS jobs (Discord, dynamic DNS), on Linux: not here yet. Nothing is queued or sent, and
// the settings commands say so instead of pretending to save.
#include "platform/cyd/web.h"

#if AXIS_WEB
namespace web {

void     begin() {}
void     tick() {}
void     setMain(bool) {}
void     setDoorRunning(bool) {}
bool     busy() { return false; }
bool     working() { return false; }
uint32_t droppedLines() { return 0; }
uint32_t gatedChecks() { return 0; }
uint32_t gatedLargestKb() { return 0; }
uint32_t startFails() { return 0; }
uint32_t startLargestKb() { return 0; }
uint32_t stackMark() { return 0; }
void     announce(const String&) {}
bool     setDiscord(const String&) { return false; }
bool     setDdns(const String&) { return false; }
bool     setNoip(const String&) { return false; }
void     ddnsSoon() {}
void     noipNow() {}
String   status() { return String("web jobs (Discord, DNS): not in the Linux program yet"); }
String   ping() { return String("web jobs: not in the Linux program yet"); }

}  // namespace web
#endif  // AXIS_WEB
