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

namespace web {

void begin();                                     // load the settings; starts the task when anything is set
void setMain(bool isMain);                        // only the main announces and updates DNS
void setDoorRunning(bool on);                     // a door has the heap: hold HTTPS until it returns
bool busy();                                      // the web task is inside a request right now
void announce(const String& line);                // queue a Discord line (dropped when the queue is full)
bool setDiscord(const String& webhookUrl);        // "" turns it off
bool setDdns(const String& spec);                 // "domain|host|key|secret", "" turns it off
bool setNoip(const String& spec);                 // No-IP: "host|username|password", "" turns it off
void ddnsSoon();                                  // bench: the web task checks DNS on its next turn
void noipNow();                                   // bench: update No-IP right now (blocking)
String status();                                  // for the serial console: what's set (no secrets)
String ping();                                    // bench: the public IP over HTTPS (proves TLS and the roots), blocking

}  // namespace web
