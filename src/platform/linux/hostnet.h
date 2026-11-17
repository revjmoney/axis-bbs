// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The Linux network layer's own calls (the Arduino stand-ins themselves are in arduino/WiFi*.h, bodies in
// hostnet.cpp).
#pragma once
#include <cstdint>

class NetGate;

// Once, after hostConfLoad() and before setup(): reads the ports and gate limits from axis.conf and raises
// RLIMIT_NOFILE to its hard limit (a warning when that is below 256).
void hostnetStart();

// Between two loop() turns: runs the callbacks of DNS lookups that have finished.
void hostnetPoll();

// For the radio modem (plan task 4): a call it answered becomes a telnet caller. `fd` is any connected stream
// socket (a socketpair end, say); from here on it belongs to the network layer, which makes it non-blocking and
// close-on-exec and closes it when the caller is gone. It comes out of the telnet server's hasClient()/available()
// like a TCP caller, with remoteIP() == fakeIp (network byte order, as IPAddress's uint32_t), and it does not pass
// the accept gate (a radio caller is never refused or counted there). The board's LoginGuard still sees fakeIp.
void hostnetInjectClient(int fd, uint32_t fakeIp);

// The accept gate every TCP caller passes (for logs and the tests' view of it).
NetGate& hostGate();
