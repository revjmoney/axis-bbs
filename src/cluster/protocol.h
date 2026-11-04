// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The words AXIS boards say to each other. Discovery is one line per UDP datagram on port 2325
// ("AXIS? <id>" asks who is main, "AXIS! <ip> <epoch>" answers). Between a follower and the main
// there is one TCP link on port 2324 carrying frames: a 4-byte length (little endian), a type byte,
// then the body, which is plain text fields separated by 0x1F (so handles and chat can hold spaces).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace axis::cluster {

constexpr uint16_t kDiscoveryPort = 2325, kLinkPort = 2324;
constexpr uint32_t kMaxFrame = 8 * 1024;  // file pieces are 2 KB; anything bigger is not ours
constexpr char     kFieldSep = '\x1f';

enum class Frame : uint8_t {
    Hello = 1,      // follower -> main: id, node number it had before (0 = none), has a user replica, has an SD card, its IP
    Welcome = 2,    // main -> follower: node number, epoch
    Heartbeat = 3,  // main -> all: epoch, then per node: number, id, ip, handle, activity
    Status = 4,     // follower -> main: handle, activity
    Chat = 5,       // either way: node, handle, text, page (0/1)
    Users = 6,      // main -> all: the whole user base (users.dat text)
    UserChange = 7, // follower -> main: one user block in users.dat form
    FileReq = 8,    // follower -> main: request id, operation, arguments
    FileResp = 9,   // main -> follower: request id, ok, data
    Yield = 10,     // main -> follower: you have the SD card, be main (this one has none)
    Settings = 11,  // main -> follower: the BBS settings (axis.ini text), so the cluster is one BBS
};

std::string              frame(Frame type, const std::vector<std::string>& fields);
std::vector<std::string> fields(const std::string& body);

// Pulls whole frames out of a byte stream; false when no complete frame is waiting, or (bad set)
// the stream is not one of ours.
class FrameReader {
public:
    void add(const std::string& bytes) { buf_ += bytes; }
    bool next(Frame& type, std::string& body);
    bool bad() const { return bad_; }

private:
    std::string buf_;
    bool        bad_ = false;
};

}  // namespace axis::cluster
