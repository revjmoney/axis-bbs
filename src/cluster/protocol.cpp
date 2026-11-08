// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")
#include "cluster/protocol.h"

namespace axis::cluster {
namespace {
constexpr char kSep = '\x1f';
bool knownFrame(uint8_t t) { return t >= (uint8_t)Frame::Hello && t <= (uint8_t)Frame::CardInfo; }
}  // namespace

std::string frame(Frame type, const std::vector<std::string>& f) {
    size_t len = f.empty() ? 0 : f.size() - 1;
    for (const std::string& s : f) {
        if (s.size() > kMaxFrame || len > kMaxFrame - s.size()) return {};
        len += s.size();
    }
    if (len + 1 > kMaxFrame) return {};
    const uint32_t n = (uint32_t)len + 1;
    std::string out;
    out.reserve(5 + len);
    for (int i = 0; i < 4; ++i) out += (char)(n >> (8 * i));
    out += (char)type;
    for (size_t i = 0; i < f.size(); ++i) {
        if (i) out += kSep;
        out += f[i];
    }
    return out;
}

std::vector<std::string> fields(const std::string& body) {
    std::vector<std::string> v;
    size_t pos = 0;
    for (;;) {
        const size_t sep = body.find(kSep, pos);
        v.push_back(body.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos));
        if (sep == std::string::npos) return v;
        pos = sep + 1;
    }
}

void FrameReader::add(const std::string& bytes) {
    if (bad_) return;
    if (bytes.size() > kMaxBuffered || buf_.size() > kMaxBuffered - bytes.size()) {
        bad_ = true;
        buf_.clear();
        return;
    }
    buf_ += bytes;
}

bool FrameReader::next(Frame& type, std::string& body) {
    if (bad_ || buf_.size() < 5) return false;
    uint32_t n = 0;
    for (int i = 0; i < 4; ++i) n |= (uint32_t)(uint8_t)buf_[i] << (8 * i);
    if (n == 0 || n > kMaxFrame) {
        bad_ = true;
        buf_.clear();
        return false;
    }
    if (buf_.size() < 4 + n) return false;
    const uint8_t rawType = (uint8_t)buf_[4];
    if (!knownFrame(rawType)) {
        bad_ = true;
        buf_.clear();
        return false;
    }
    type = (Frame)rawType;
    body = buf_.substr(5, n - 1);
    buf_.erase(0, 4 + n);
    if (buf_.empty()) std::string().swap(buf_);
    return true;
}
}  // namespace axis::cluster
