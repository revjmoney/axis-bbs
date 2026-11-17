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

// The program is stopping (main thread, between loop() turns). No call is answered from now on; a call that is up is
// hung up at once (its socket closed, AXIS's unsent bytes dropped), then the usual guard, +++, guard, ATH runs as
// modemLinePoll() keeps being called. With modem_guard_ms at its 1200 default that is about 2.5 s.
void modemLineHangUp();

// A call is up or being hung up (the shutdown keeps polling until this is false, or its time runs out).
bool modemLineBusy();

// Just before the exit: an ATH still queued for the tty is written, and an unfinished hang-up is logged.
void modemLineFinish();
