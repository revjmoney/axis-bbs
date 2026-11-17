// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Net Files fetch calls. See fetch.h for the wire and who checks what.
#include "net/fetch.h"
#if AXIS_NETS && AXIS_TRANSFERS

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <new>

#include "net/crc.h"

namespace axis::net {

namespace {

bool hex16(const std::string& s, size_t at) {
    if (s.size() < at + 16) return false;
    for (size_t i = at; i < at + 16; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

// Appends the words joined by sep, then end (one append each, not a chain of temporary strings).
void addLine(std::string& out, std::initializer_list<const std::string*> words, char sep, const char* end) {
    bool first = true;
    for (const std::string* w : words) {
        if (!first) out += sep;
        first = false;
        out += *w;
    }
    out += end;
}

std::string hiText(const std::string& dial, const std::string& serv, const std::string& board) {
    std::string s = "axisfile|";
    addLine(s, {&dial, &serv, &board}, '|', "");
    return s;
}

// One CR LF line at a time from p; false (and the buffer dropped) once a line runs past kFetchMaxLine.
// at: where the scan resumes; got: a whole line is in buf (its CR dropped).
bool takeLine(std::string& buf, const char* p, size_t n, size_t& at, bool& got) {
    got = false;
    const char* nl = (const char*)std::memchr(p + at, '\n', n - at);
    size_t      seg = nl ? (size_t)(nl - (p + at)) : n - at;
    if (buf.size() + seg > kFetchMaxLine) {
        std::string().swap(buf);
        return false;
    }
    buf.append(p + at, seg);
    at += seg;
    if (nl) {
        at++;
        if (!buf.empty() && buf.back() == '\r') buf.pop_back();
        got = true;
    }
    return true;
}

}  // namespace

// ------------------------------------------------------------------ server
FetchServer::FetchServer(const std::string& myBoard, const KeyPair& key, const std::string& servNonce16,
                         std::function<ServeFile(const NetFetch&)> check)
    : board_(myBoard), serv_(servNonce16), key_(key), check_(std::move(check)) {}

FetchServer::~FetchServer() { wipe(key_); }

void FetchServer::refuse(const char* word) {
    out_ += "NO ";
    out_ += word;
    out_ += "\r\n";
    why_ = word;
    st_ = St::Failed;
}

void FetchServer::feed(const char* p, size_t n) {
    size_t at = 0;
    if (n && st_ == St::Serving) moved_ = true;  // a keepalive: dropped, but the dialer is there
    while (at < n && (st_ == St::Hello || st_ == St::Get)) {  // nothing is read once serving
        bool got;
        if (!takeLine(in_, p, n, at, got)) return refuse("bad");
        if (!got) break;
        std::string l;
        l.swap(in_);
        line(l);
    }
}

void FetchServer::line(const std::string& l) {
    if (st_ == St::Hello) {
        if (l.size() != 27 || l.compare(0, 11, "AXISFILE 1 ") != 0 || !hex16(l, 11)) return refuse("bad");
        dial_ = l.substr(11);
        std::string sig = signHex(key_, hiText(dial_, serv_, board_));
        wipe(key_);  // HI is the only thing this call signs
        out_ += "HI ";
        addLine(out_, {&board_, &serv_, &sig}, ' ', "\r\n");
        st_ = St::Get;
        return;
    }
    NetFetch f;
    if (l.compare(0, 4, "GET ") != 0 || !decode(std::string_view(l).substr(4), f)) return refuse("bad");
    if (f.dialNonce != dial_ || f.servNonce != serv_) return refuse("sig");  // another call's request
    file_ = check_(f);
    if (!file_.ok) {
        file_.read = nullptr;
        return refuse(file_.no ? file_.no : "bad");
    }
    if (f.offset > file_.size || !file_.read) {
        file_.read = nullptr;
        return refuse("size");
    }
    pos_ = f.offset;
    std::string n = std::to_string(file_.size - pos_);
    out_ += "OK ";
    addLine(out_, {&n}, ' ', "\r\n");
    st_ = St::Serving;
}

std::string FetchServer::takeOut(size_t room) {
    std::string s = out_.substr(0, room);
    out_.erase(0, s.size());
    if (st_ == St::Serving && out_.empty()) {
        size_t n = std::min({room - s.size(), kFetchChunk, (size_t)(file_.size - pos_)});
        if (n) {
            size_t was = s.size();
            s.resize(was + n);
            if (!file_.read(pos_, (uint8_t*)&s[was], n)) {
                s.resize(was);
                file_.read = nullptr;
                why_ = "read";
                st_ = St::Failed;  // the dialer sees the call close short
                return s;
            }
            pos_ += (uint32_t)n;
        }
        if (pos_ == file_.size) {
            file_.read = nullptr;  // lets the host's file go
            st_ = St::Done;
        }
    }
    if (!s.empty()) moved_ = true;
    return s;
}

void FetchServer::tick(uint32_t now) {
    if (!started_) {
        started_ = true;
        startMs_ = lastMs_ = now;
    }
    if (moved_) lastMs_ = now;
    moved_ = false;
    if (st_ == St::Done || st_ == St::Failed) return;
    bool late = st_ == St::Serving ? now - lastMs_ > kFetchStallMs : now - startMs_ > kFetchConnectMs;
    if (late) {
        why_ = st_ == St::Serving ? "stall" : "timeout";
        file_.read = nullptr;
        st_ = St::Failed;
    }
}

bool FetchServer::finished() const { return (st_ == St::Done || st_ == St::Failed) && out_.empty(); }
bool FetchServer::failed() const { return st_ == St::Failed; }

// ------------------------------------------------------------------ client
FetchClient::FetchClient(const NetListing& l, const std::string& homePub, const std::string& handle, const KeyPair& me,
                         const std::string& myBoard, const std::string& dialNonce16, uint32_t offset,
                         uint32_t crcState, uint32_t crcPos)
    : me_(me), board_(l.board), homePub_(homePub), handle_(handle), myBoard_(myBoard), dial_(dialNonce16),
      id_(listingId(l)), size_(l.size), start_(offset), hi_(offset), listCrc_(l.crc), crc_(crcState),
      crcPos_(crcPos), crcOn_(crcPos == offset && crcPos <= l.size), want_(offset) {
    hash(nullptr, 0);  // the checkpoint at 0, for a download from the start
    if (offset > l.size) {
        fail("size");
        return;
    }
    out_ = "AXISFILE 1 ";
    addLine(out_, {&dial_}, ' ', "\r\n");
}

FetchClient::~FetchClient() { wipe(me_); }

void FetchClient::fail(const char* why) {
    why_ = why;
    st_ = St::Failed;
    wipe(me_);
    ring_.reset();
    std::string().swap(in_);
}

size_t FetchClient::room() const {
    if (st_ == St::Hello || st_ == St::Asked) return kFetchWindow;  // whatever trails OK still fits the ring
    if (st_ != St::Streaming) return 0;
    uint64_t cap = std::min<uint64_t>((uint64_t)want_ + kFetchWindow, size_);
    return cap > hi_ ? (size_t)std::min<uint64_t>(cap - hi_, kFetchWindow) : 0;
}

void FetchClient::hash(const uint8_t* p, size_t n) {
    if (!crcOn_) return;
    for (;;) {
        if (marks_.state.size() == kFetchCrcMarks) {  // full: every other one goes, the spacing doubles
            for (size_t i = 0; i < kFetchCrcMarks / 2; i++) marks_.state[i] = marks_.state[2 * i];
            marks_.state.resize(kFetchCrcMarks / 2);
            marks_.step *= 2;
        }
        uint64_t next = (uint64_t)marks_.state.size() * marks_.step;  // the next checkpoint's offset
        if (next == crcPos_) {
            marks_.state.push_back(crc_);
            continue;
        }
        size_t k = next > crcPos_ && next - crcPos_ < n ? (size_t)(next - crcPos_) : n;
        if (!k) return;
        crc_ = crc32Update(crc_, p, k);
        crcPos_ += (uint32_t)k;
        p += k;
        n -= k;
    }
}

bool FetchClient::checkpoint(uint32_t off, uint32_t& at, uint32_t& state) const {
    if (!crcOn_ || marks_.state.empty()) return false;
    size_t i = std::min<size_t>(off / marks_.step, marks_.state.size() - 1);
    at = (uint32_t)(i * marks_.step);
    state = marks_.state[i];
    return true;
}

void FetchClient::giveMarks(CrcMarks m) {
    if (!crcOn_ || !m.step || crcPos_ % m.step || crcPos_ / m.step >= m.state.size()) return;  // not one of its checkpoints
    m.state.resize(crcPos_ / m.step);  // up to its start; hash() takes the one at the start again
    marks_ = std::move(m);
    hash(nullptr, 0);
}

void FetchClient::arrived() {
    if (crcOn_ && crcPos_ == size_ && ~crc_ != listCrc_) return fail("crc");  // the last block never goes out
    if (crcPos_ != size_) crcOn_ = false;  // bytes were skipped: the end cannot be checked
    st_ = St::Done;
}

size_t FetchClient::feed(const char* p, size_t n) {
    size_t at = 0;
    while (at < n && (st_ == St::Hello || st_ == St::Asked)) {
        bool got;
        if (!takeLine(in_, p, n, at, got)) {
            fail("bad");
            return at;
        }
        if (!got) break;
        std::string l;
        l.swap(in_);
        line(l);
    }
    if (st_ == St::Done) return n;  // anything past the promised bytes is dropped (the data is whole)
    if (at == n || st_ != St::Streaming) return at;
    size_t take = std::min(n - at, room());
    hash((const uint8_t*)p + at, take);  // (crcPos_ is hi_ while the crc is on: one pass, in order)
    for (size_t done = 0; done < take;) {
        size_t i = hi_ & (kFetchWindow - 1), k = std::min(take - done, kFetchWindow - i);
        std::memcpy(ring_.get() + i, p + at + done, k);
        done += k;
        hi_ += (uint32_t)k;
    }
    if (take) heard_ = true;
    if (hi_ == size_) arrived();
    return at + take;
}

void FetchClient::line(const std::string& l) {
    heard_ = true;
    if (l.compare(0, 3, "NO ") == 0) {
        static const char* const kWords[] = {"busy", "gone", "size", "member", "banned", "sig", "bad"};
        for (const char* w : kWords)
            if (l.compare(3, std::string::npos, w) == 0) return fail(w);
        return fail("bad");
    }
    if (st_ == St::Hello) {
        // HI <board16> <nonce16> <sig128>
        if (l.size() != 165 || l.compare(0, 3, "HI ") != 0 || l[19] != ' ' || l[36] != ' ' || !hex16(l, 20))
            return fail("bad");
        std::string serv = l.substr(20, 16);
        if (l.compare(3, 16, board_) != 0 || boardId(homePub_) != board_ ||
            !verifyHex(homePub_, hiText(dial_, serv, board_), l.substr(37)))
            return fail("sig");
        NetFetch f;
        f.board = myBoard_;
        f.listing = id_;
        f.handle = handle_;
        f.dialNonce = dial_;
        f.servNonce = serv;
        f.offset = start_;
        bool signedOk = signFetch(f, me_);
        wipe(me_);  // the GET is the only thing this call signs
        std::string g = signedOk ? encode(f) : std::string();
        if (g.empty()) return fail("bad");
        out_ += "GET ";
        addLine(out_, {&g}, ' ', "\r\n");
        st_ = St::Asked;
        return;
    }
    // OK <n>, n exactly the bytes from the offset to the end of the listing's file
    if (l.size() < 4 || l.size() > 13 || l.compare(0, 3, "OK ") != 0) return fail("bad");
    uint64_t v = 0;
    for (size_t i = 3; i < l.size(); i++) {
        if (l[i] < '0' || l[i] > '9') return fail("bad");
        v = v * 10 + (uint64_t)(l[i] - '0');
    }
    if (v != (uint64_t)(size_ - start_)) return fail("size");
    std::string().swap(in_);
    if (v == 0) return arrived();
    ring_.reset(new (std::nothrow) uint8_t[kFetchWindow]);
    if (!ring_) return fail("memory");
    st_ = St::Streaming;
}

std::string FetchClient::takeOut() {
    std::string s;
    s.swap(out_);
    return s;
}

void FetchClient::tick(uint32_t now) {
    if (!started_) {
        started_ = true;
        startMs_ = lastMs_ = keepMs_ = now;
    }
    if (st_ == St::Done || st_ == St::Failed) return;
    if (st_ != St::Streaming) {
        if (now - startMs_ > kFetchConnectMs) fail("stall");
        return;
    }
    if (now - keepMs_ >= kFetchStallMs / 2) {  // the home board's sign that this end is still here
        keepMs_ = now;
        out_ += '\n';
    }
    if (heard_ || room() == 0) lastMs_ = now;  // a full ring is the caller's pace, not a dead call
    heard_ = false;
    if (now - lastMs_ > kFetchStallMs) fail("stall");
}

bool FetchClient::ready(uint32_t off, size_t n) const {
    if (st_ != St::Streaming && st_ != St::Done) return false;
    want_ = off;
    if (off >= size_) return true;
    if (n > size_ - off) n = size_ - off;
    return off >= low() && (uint64_t)off + n <= hi_;
}

bool FetchClient::read(uint32_t off, uint8_t* buf, size_t n) {
    if (!ring_ || (st_ != St::Streaming && st_ != St::Done)) return n == 0;
    want_ = off;
    if (off < low() || (uint64_t)off + n > hi_) return false;
    for (size_t done = 0; done < n;) {
        size_t i = (off + done) & (kFetchWindow - 1), k = std::min(n - done, kFetchWindow - i);
        std::memcpy(buf + done, ring_.get() + i, k);
        done += k;
    }
    return true;
}

bool FetchClient::behind(uint32_t off) const {
    if (off >= size_) return false;
    return off < low() || (uint64_t)off >= (uint64_t)std::max(hi_, want_) + kFetchWindow;  // (want_: an aim() past what came)
}

}  // namespace axis::net

#endif  // AXIS_NETS && AXIS_TRANSFERS
