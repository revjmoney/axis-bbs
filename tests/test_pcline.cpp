// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The PC caller line (src/pcapp/pcline.h) over a real socketpair: a caller who stops reading fills the socket and goes on
// hold (its screen waits in its own backlog), while another caller's line still flushes clean. No real timing is needed:
// the stalled line's backlog is kept well under kHoldMax, so flush never blocks waiting it down.
#include <sys/socket.h>
#include <unistd.h>

#include <string>

#include "doctest.h"
#include "net/telnetstream.h"
#include "pcapp/pcline.h"

using namespace axis;

namespace {
// Make the kernel buffers small so a non-reading peer fills quickly (the backlog then lives in the PcLine, not the socket).
void shrink(int fd, int opt) {
    int sz = 2048;
    setsockopt(fd, SOL_SOCKET, opt, &sz, sizeof sz);
}
}  // namespace

TEST_CASE("a caller who stops reading does not stall another caller's screen") {
    int stalled[2], reader[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, stalled) == 0);
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, reader) == 0);
    shrink(stalled[0], SO_SNDBUF);
    shrink(stalled[1], SO_RCVBUF);  // stalled[1] is never read, so once both buffers are full the line must hold

    auto slow = pcapp::PcLine::fromFd(stalled[0], 0x0a000001);  // fake 10.0.0.1
    auto fast = pcapp::PcLine::fromFd(reader[0], 0x0a000002);   // fake 10.0.0.2
    slow->setHold(true);
    fast->setHold(true);

    // Fill the slow caller in small pieces until its line holds, capped so the backlog never nears kHoldMax (16 KB).
    std::string piece(512, 'x');
    bool        held = false;
    for (int i = 0; i < 24 && !held; i++) {  // at most 12 KB written, minus what the socket took: well under kHoldMax
        slow->write((const uint8_t*)piece.data(), piece.size());
        slow->flush();
        if (slow->backlog() > 0) held = true;
    }
    REQUIRE(held);
    REQUIRE(slow->backlog() > 0);
    REQUIRE(slow->backlog() <= net::TelnetStream::kHoldMax);

    // The other caller's peer reads, so its screen flushes clean in the same breath.
    std::string msg = "hello node 2\r\n";
    fast->write((const uint8_t*)msg.data(), msg.size());
    fast->flush();
    CHECK(fast->backlog() == 0);

    char    buf[64];
    ssize_t got = recv(reader[1], buf, sizeof buf, MSG_DONTWAIT);
    REQUIRE(got == (ssize_t)msg.size());
    CHECK(std::string(buf, (size_t)got) == msg);

    // The slow caller is still holding and still up; the fast one was not touched by it.
    CHECK(slow->backlog() > 0);
    CHECK(slow->carrier());
    CHECK(fast->carrier());

    // A further small write to the slow line is kept, not blocked on, and the line stays up.
    slow->write((const uint8_t*)piece.data(), piece.size());
    slow->flush();
    CHECK(slow->carrier());
    CHECK(slow->backlog() > 0);

    // Close the never-read peer before the lines go, in case hang-up pushes the backlog.
    close(stalled[1]);
    close(reader[1]);
}
