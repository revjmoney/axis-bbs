// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The words AXIS boards say to each other. Discovery is one line per UDP datagram on port 2325
// ("AXIS? <id>" asks who is main, "AXIS! <ip> <epoch>" answers). TCP on port 2324 carries
// length-prefixed frames. This is a framing limit, not peer authentication or encryption.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace axis::cluster {

constexpr uint16_t kDiscoveryPort = 2325, kLinkPort = 2324;
constexpr uint32_t kMaxFrame = 8 * 1024;
constexpr size_t   kMaxBuffered = 32 * 1024;
constexpr char     kFieldSep = '\x1f';

enum class Frame : uint8_t {
    Hello = 1, Welcome = 2, Heartbeat = 3, Status = 4, Chat = 5, Users = 6, UserChange = 7,
    FileReq = 8, FileResp = 9, Yield = 10, Settings = 11, Renumber = 12, MainReq = 13,
    MainResp = 14, CardInfo = 15,
};

std::string              frame(Frame type, const std::vector<std::string>& fields);
std::vector<std::string> fields(const std::string& body);

class FrameReader {
public:
    void add(const std::string& bytes);
    bool next(Frame& type, std::string& body);
    bool bad() const { return bad_; }

private:
    std::string buf_;
    bool        bad_ = false;
};

}  // namespace axis::cluster
