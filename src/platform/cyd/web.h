// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's own outbound HTTPS, on a background task so a slow server never stalls a caller:
// Discord announcements (a channel webhook) and dynamic DNS for a GoDaddy subdomain or a No-IP
// host (the BBS's public address follows the home IP). The board that holds the settings does the work: a
// headless node (a TLS handshake needs about 45 KB, more than a busy CYD main has free). The webhook URL and the
// GoDaddy key are the sysop's secrets: they live in this board's own storage (NVS "web"), set over
// the serial console, never in axis.ini (which travels to every board).
#pragma once
#include <Arduino.h>

#include "bbs/features.h"

namespace web {

#if AXIS_WEB

void begin();                                     // load the settings
void tick();                                      // the main loop, often: starts the task when a check or a line is waiting
void setMain(bool isMain);                        // only the main announces and updates DNS
void setDoorRunning(bool on);                     // a door has the heap: hold HTTPS until it returns
bool busy();                                      // the web task is inside a request right now
bool working();                                   // the task is up, or a DNS check or a Discord line waits for it
uint32_t droppedLines();                          // Discord lines dropped because TLS did not fit (not doors)
uint32_t gatedChecks();                           // DNS / No-IP checks held back for lack of memory
uint32_t gatedLargestKb();                        // largest block (KB) the last held check saw
uint32_t startFails();                            // task starts refused for lack of memory (the work waits)
uint32_t startLargestKb();                        // largest block (KB) the last refused start saw
uint32_t stackMark();                             // the task's lowest stack high-water mark so far (0: never ran)
void announce(const String& line);                // queue a Discord line (dropped when the queue is full)
bool setDiscord(const String& webhookUrl);        // "" turns it off
bool setDdns(const String& spec);                 // "domain|host|key|secret", "" turns it off
bool setNoip(const String& spec);                 // No-IP: "host|username|password", "" turns it off
void ddnsSoon();                                  // bench: the next tick hands the web task a DNS check
void noipNow();                                   // bench: update No-IP right now (blocking)
String status();                                  // for the serial console: what's set (no secrets)
String ping();                                    // bench: the public IP over HTTPS (proves TLS and the roots), blocking

#else  // built without the web jobs (AXIS_WEB): no task, no Discord, no DNS. The settings saved in NVS "web" are never
       // opened here, so they stay as they are for a later build that has them.
inline void     begin() {}
inline void     tick() {}
inline void     setMain(bool) {}
inline void     setDoorRunning(bool) {}
inline bool     busy() { return false; }
inline bool     working() { return false; }
inline uint32_t droppedLines() { return 0; }
inline uint32_t gatedChecks() { return 0; }
inline uint32_t gatedLargestKb() { return 0; }
inline uint32_t startFails() { return 0; }
inline uint32_t startLargestKb() { return 0; }
inline uint32_t stackMark() { return 0; }
inline void     announce(const String&) {}
#endif  // AXIS_WEB

}  // namespace web
