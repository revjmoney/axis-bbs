// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM on a serial port: radio calls it answers become telnet
// callers. A port of the GHOSTBOARD's tools/lfd3k_bridge.py into the program, run between loop() turns and from
// delay() so it never blocks the board's loop:
//   RING: send ATA. CONNECT ...: a socketpair, one end handed to the telnet server (hostnetInjectClient, from
//   127.0.0.2), bytes pumped both ways. AXIS's telnet requests are refused for the caller (a plain terminal) and IAC
//   is taken out of what goes over the air. After AXIS's opening negotiation it offers LFD3K-LINK (WILL 90); once
//   AXIS says DO 90 a report "v=1 modem=<station> kind=radio caller=- tier=SFn bps=n" (from the CONNECT line) goes
//   at once and every 10 s, so main.cpp tags the caller RADIO SFn. NO CARRIER ends the call; AXIS hanging up sends
//   +++ with the guard time either side, then ATH.
// The tty is 115200 8N1 raw, DTR and RTS raised and left raised (dropping them one at a time resets the V3), and no
// modem setting is changed (AT&V is read once, for the station name). A tty that vanishes or errors ends any call,
// logs "axis: modem gone" and is opened again every 5 s.
#pragma once
#include <cstdint>
#include <string>

// Once, before setup(): `path` is the tty (--modem or axis.conf's modem=); empty means no modem.
void modemLineStart(const std::string& path);

// Between loop() turns (and from delay()): never waits.
void modemLinePoll(uint32_t nowMs);
