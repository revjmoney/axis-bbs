// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "cluster/protocol.h"

namespace axis::cluster {

namespace {
constexpr char kSep = '\x1f';
bool known(uint8_t t) { return t >= (uint8_t)Frame::Hello && t <= (uint8_t)Frame::Auth; }  // keep in step with Frame
}  // namespace

std::string frame(Frame type, const std::vector<std::string>& f) {
    size_t len = f.empty() ? 0 : f.size() - 1;  // one allocation, no body copy: the main joins a board on a fragmented heap
    for (const std::string& s : f) len += s.size();
    uint32_t    n = (uint32_t)len + 1;
    std::string out;
    out.reserve(5 + len + kTagBytes);  // and the tag put() adds, so it never grows (doubling) the buffer
    for (int i = 0; i < 4; i++) out += (char)(n >> (8 * i));
    out += (char)type;
    for (size_t i = 0; i < f.size(); i++) {
        if (i) out += kSep;
        out += f[i];
    }
    return out;
}

std::vector<std::string> fields(const std::string& body) {
    std::vector<std::string> v;
    size_t                   pos = 0;
    for (;;) {
        size_t sep = body.find(kSep, pos);
        v.push_back(body.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos));
        if (sep == std::string::npos) return v;
        pos = sep + 1;
    }
}

void FrameReader::add(const std::string& bytes) {
    if (bad_) return;
    if (bytes.size() > kMaxBuffered - buf_.size()) {  // nobody is reading this, or it is not ours
        fail();
        return;
    }
    buf_ += bytes;
}

void FrameReader::fail() {
    bad_ = true;
    std::string().swap(buf_);  // the memory back now, not when the link is torn down
}

bool FrameReader::next(Frame& type, std::string& body) {
    if (bad_ || buf_.size() < 5) return false;
    uint32_t n = 0;
    for (int i = 0; i < 4; i++) n |= (uint32_t)(uint8_t)buf_[i] << (8 * i);
    if (n == 0 || n > kMaxFrame || !known((uint8_t)buf_[4])) {
        fail();
        return false;
    }
    if (buf_.size() < 4 + n) return false;
    type = (Frame)(uint8_t)buf_[4];
    body = buf_.substr(5, n - 1);
    buf_.erase(0, 4 + n);
    if (buf_.empty()) std::string().swap(buf_);  // drained: give the memory back (a 26 KB heap can't keep peaks)
    return true;
}

}  // namespace axis::cluster
