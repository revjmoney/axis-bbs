// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "cluster/protocol.h"

using namespace axis::cluster;

TEST_CASE("cluster frame reader rejects oversized and unknown frames") {
    FrameReader oversized;
    std::string header;
    const uint32_t len = kMaxFrame + 1;
    for (int i = 0; i < 4; ++i) header += (char)(len >> (i * 8));
    header += (char)Frame::Chat;
    oversized.add(header);
    Frame type;
    std::string body;
    CHECK_FALSE(oversized.next(type, body));
    CHECK(oversized.bad());

    FrameReader unknown;
    unknown.add(std::string("\1\0\0\0\xff", 5));
    CHECK_FALSE(unknown.next(type, body));
    CHECK(unknown.bad());
}

TEST_CASE("cluster frame reader bounds buffered input") {
    FrameReader reader;
    reader.add(std::string(kMaxBuffered, 'x'));
    reader.add("x");
    Frame type;
    std::string body;
    CHECK_FALSE(reader.next(type, body));
    CHECK(reader.bad());
}

TEST_CASE("cluster frame encoder refuses payloads larger than the frame limit") {
    CHECK(frame(Frame::Chat, {std::string(kMaxFrame, 'x')}).empty());
}
