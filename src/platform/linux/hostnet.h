// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The Linux network layer's own calls (the Arduino stand-ins themselves are in arduino/WiFi*.h, bodies in
// hostnet.cpp).
#pragma once
#include <cstddef>
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

// The program is stopping (startmode.h): from now on no new caller is taken, radio calls not yet answered are
// dropped, and every caller's socket is shut both ways. main.cpp sees each one gone on its next turn and hangs it up
// through its own path, which lets go of the socket.
void hostnetHangUpCallers();

// Callers' sockets still open (main.cpp has not let go of them yet).
size_t hostnetCallersOpen();

// The accept gate every TCP caller passes (for logs and the tests' view of it).
NetGate& hostGate();

// The solo watch heard a main (clusterhost.cpp): the public listener closes now and is not opened again in this run
// (the LAN port stays). The cluster profiles never open it at all (hostnetStart logs that public_port is off).
void hostnetClosePublic();

// On: every new telnet caller (and a radio call that connects meanwhile) is told "This BBS is joining its cluster.
// Please call back in a few minutes." and closed. Off: callers are taken again.
void hostnetHold(bool on);

// The port to dial when this program, as main, passes a caller to another node: here the board's telnet port 23 shows
// as lan_port (the configLoad wrap), but the boards (and this program, clustered) answer on 23.
uint16_t hostnetPassPort(uint16_t port);

// Callers of the BBS itself still open: telnet and radio, not the panel's connections (the drain waits for these).
size_t hostnetBbsCallersOpen();
