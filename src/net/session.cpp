// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// NetSession: the board to board sync state machine (handshake, state exchange, history for a
// new board, outbound log entries with acks). See session.h and the wire notes below.
//
// Wire, one line each, ending with '\n'. The handshake:
//   dialer   -> AXISNET 2 <card> <dialer nonce>
//   answerer -> AXISNET 2 <card> <answerer nonce>     (once the dialer's hello checks out)
//   dialer   -> PROOF <sig of transcript D>
//   answerer -> PROOF <sig of transcript A>           (only once the dialer's proof checks out)
// where transcript R = "AXISNET2|R|<dialer nonce>|<answerer nonce>|<dialer board>|<answerer board>".
// Then in each direction, in this order:
//   SEQ <highSeq> [WANT 30]
//   S <record>* SEND           (the snapshot, maybe empty)
//   H <post>* HEND             (only if the peer sent WANT)
//   E <index> <record>*        (answered by ACK <index> or NAK <index> <reason>)
//   DONE
// BYE <reason> ends the session at any point.
#include "net/session.h"

#include <cstring>

namespace axis::net {

namespace {

bool isHex(const std::string& s, size_t n) {
    if (s.size() != n) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

// Digits only, no leading zero (except "0" itself), fits in 32 bits.
bool parseNum(const std::string& s, uint32_t& out) {
    if (s.empty() || s.size() > 10 || (s.size() > 1 && s[0] == '0')) return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (uint64_t)(c - '0');
    }
    if (v > 0xFFFFFFFFull) return false;
    out = (uint32_t)v;
    return true;
}

// Splits "VERB rest" at the first space. rest is empty when there is no space.
void verbOf(const std::string& l, std::string& verb, std::string& rest) {
    size_t sp = l.find(' ');
    if (sp == std::string::npos) {
        verb = l;
        rest.clear();
    } else {
        verb = l.substr(0, sp);
        rest = l.substr(sp + 1);
    }
}

const char* tookName(NetCore::Took t) {
    switch (t) {
        case NetCore::Took::Rejected: return "Rejected";
        case NetCore::Took::Banned: return "Banned";
        case NetCore::Took::Limited: return "Limited";
        default: return "bad";
    }
}

// A record this side may put on the wire: with any prefix (at most 32 characters) it stays within
// what a receiver takes.
bool fits(const std::string& rec) { return !rec.empty() && rec.size() <= kNetMaxFrame; }

}  // namespace

NetSession::NetSession(NetCore& core, const NetCard& me, const KeyPair& signer, const std::string& nonceHex16,
                       bool dialer, const std::string& expectedBoard)
    : core_(core), me_(me), signer_(signer), nonce_(nonceHex16), dialer_(dialer),
      expected_(dialer ? expectedBoard : std::string()) {
    if (!isHex(nonce_, 16)) {
        fail("bad nonce", nullptr);
        return;
    }
    if (me_.pub != toHex(signer_.pub, 32) || me_.board != boardId(me_.pub) || !fits(encode(me_))) {
        fail("bad own card", nullptr);
        return;
    }
    if (dialer_) sendHello();
}

void NetSession::send(const std::string& l) {
    out_ += l;
    out_ += '\n';
}

void NetSession::sendHello() { send("AXISNET 2 " + encode(me_) + " " + nonce_); }

std::string NetSession::takeOut() {
    std::string o;
    o.swap(out_);
    return o;
}

void NetSession::fail(const char* why, const char* bye) {
    if (failed_) return;
    failed_ = true;
    why_ = why;
    if (bye) send(std::string("BYE ") + bye);
    // Let go of everything the session holds.
    std::string().swap(in_);
    std::vector<std::string>().swap(snap_);
    std::deque<std::pair<uint32_t, std::string>>().swap(fetched_);
    std::deque<std::pair<uint32_t, bool>>().swap(inFlight_);
    hist_ = HistoryCursor();
    peerCard_ = NetCard();
}

// Unsent output past kSyncOutMax: the peer (or the host) is not taking it. Fails the session.
bool NetSession::outOverflow() {
    if (failed_ || out_.size() <= kSyncOutMax) return false;
    fail("peer not reading", nullptr);
    return true;
}

void NetSession::feed(const char* p, size_t n) {
    if (failed_ || n == 0) return;
    size_t i = 0;
    while (i < n && !failed_) {
        const char* nl = (const char*)std::memchr(p + i, '\n', n - i);
        size_t seg = nl ? (size_t)(nl - (p + i)) : n - i;
        if (in_.size() + seg > kSyncMaxLine) {  // never hold more than one line's worth
            std::string().swap(in_);
            fail("frame too big", "big");
            return;
        }
        in_.append(p + i, seg);
        i += seg;
        if (!nl) break;
        i++;  // the newline
        std::string l;
        l.swap(in_);  // the buffer is empty again before the line is handled
        if (!l.empty() && l.back() == '\r') l.pop_back();  // records never hold a CR
        line(l);
        if (outOverflow()) return;
    }
}

void NetSession::line(std::string& l) {
    heard_ = true;  // only complete lines count against the silence timer
    if (++linesRx_ > kSyncMaxLines) return fail("too many lines", "limit");
    if (l.compare(0, 4, "BYE ") == 0 || l == "BYE") {
        std::string reason = l.size() > 4 ? l.substr(4, 32) : "";
        for (char& c : reason)
            if (c < 0x20 || c > 0x7E) c = '?';
        fail("bye", nullptr);
        why_ = "bye " + reason;
        return;
    }
    switch (phase_) {
        case Phase::Hello:
            if (!hello(l)) return;
            if (dialer_) send("PROOF " + signHex(signer_, transcript('D')));  // the dialer proves first
            else sendHello();  // the answerer signs nothing until the dialer's proof checks out
            phase_ = Phase::Proof;
            return;
        case Phase::Proof:
            proof(l);
            return;
        case Phase::Sync:
            syncLine(l);
            return;
    }
}

bool NetSession::hello(const std::string& l) {
    static const char kPre[] = "AXISNET 2 ";
    const size_t pre = sizeof kPre - 1;
    if (l.size() < pre + 18 || l.compare(0, pre, kPre) != 0 || l[l.size() - 17] != ' ') {
        fail("bad line", "bad");
        return false;
    }
    std::string nonce = l.substr(l.size() - 16);
    NetCard c;
    if (!isHex(nonce, 16) || !decode(l.substr(pre, l.size() - 17 - pre), c)) {
        fail("bad line", "bad");
        return false;
    }
    if (!verifyCard(c) || c.board != boardId(c.pub) || c.board == core_.board() || c.board == me_.board) {
        fail("bad card", "bad");
        return false;
    }
    if (dialer_ && !expected_.empty() && c.board != expected_) {  // not the board we dialed
        fail("wrong board", "bad");
        return false;
    }
    if (core_.state().banned(c.board)) {
        fail("banned", "banned");
        return false;
    }
    peer_ = c.board;
    peerNonce_ = nonce;
    peerCard_ = std::move(c);
    return true;
}

std::string NetSession::transcript(char role) const {
    const std::string& dNonce = dialer_ ? nonce_ : peerNonce_;
    const std::string& aNonce = dialer_ ? peerNonce_ : nonce_;
    const std::string& dBoard = dialer_ ? me_.board : peer_;
    const std::string& aBoard = dialer_ ? peer_ : me_.board;
    return std::string("AXISNET2|") + role + "|" + dNonce + "|" + aNonce + "|" + dBoard + "|" + aBoard;
}

void NetSession::proof(const std::string& l) {
    if (l.compare(0, 6, "PROOF ") != 0 || !isHex(l.substr(6), 128)) return fail("bad line", "bad");
    // The peer's proof carries the peer's role: D from a dialer, A from an answerer.
    if (!verifyHex(peerCard_.pub, transcript(dialer_ ? 'A' : 'D'), l.substr(6))) return fail("bad proof", "bad");
    if (!dialer_) send("PROOF " + signHex(signer_, transcript('A')));
    enterSync();
}

void NetSession::enterSync() {
    // Proven. Whether it was a link peer before decides the snapshot on first contact; adding it
    // first pins its card so the card is never evicted for room. A board we dialed goes into the
    // outbound pool, one that dialed us into the inbound pool.
    wasLink_ = core_.isLinkPeer(peer_);
    core_.addLinkPeer(peer_, dialer_);
    core_.takeCard(peerCard_, peer_);
    peerCard_ = NetCard();
    phase_ = Phase::Sync;
    mySeq_ = core_.state().highSeq();
    send("SEQ " + std::to_string(mySeq_));
    // WANT (or not) follows in the first tick with heap to spare: hasNetPosts reads headers.
}

void NetSession::syncLine(std::string& l) {
    std::string verb, rest;
    verbOf(l, verb, rest);
    uint32_t n = 0;
    if (verb == "SEQ") {
        if (peerSeq_ || !parseNum(rest, n)) return fail("bad line", "bad");
        peerSeq_ = true;
        sendSnap_ = n != mySeq_ || !wasLink_;  // equal seqs alone say nothing on first contact
        return;
    }
    if (!peerSeq_) return fail("bad line", "bad");  // everything else comes after the peer's SEQ
    if (verb == "ACK" || verb == "NAK") {
        std::string idx = rest, reason;
        if (verb == "NAK") verbOf(rest, idx, reason);
        return ack(idx, verb == "NAK");
    }
    if (verb == "E") {
        std::string idx, rec;
        verbOf(rest, idx, rec);
        peerDone_ = false;  // more after a DONE: a new DONE follows
        return takeEntry(idx, rec);
    }
    if (verb == "S") {
        if (peerSend_) return fail("bad line", "bad");
        return takeState(rest);
    }
    if (verb == "H") {
        if (!sentWant_ || peerHend_) return fail("bad line", "bad");  // history only when asked for
        return takeHistory(rest);
    }
    if (verb == "WANT") {
        if (peerWant_ || peerSend_ || !parseNum(rest, n) || n == 0) return fail("bad line", "bad");
        peerWant_ = true;
        histDays_ = n < kSyncHistoryDays ? n : kSyncHistoryDays;
        return;
    }
    if (!rest.empty()) return fail("bad line", "bad");
    if (verb == "SEND") {
        if (peerSend_) return fail("bad line", "bad");
        peerSend_ = true;
        return;
    }
    if (verb == "HEND") {
        if (!sentWant_ || peerHend_) return fail("bad line", "bad");
        peerHend_ = true;
        return;
    }
    if (verb == "DONE") {
        if (!peerSend_) return fail("bad line", "bad");
        peerDone_ = true;
        return;
    }
    fail("bad line", "bad");
}

void NetSession::ack(const std::string& idx, bool nak) {
    uint32_t i;
    // Answers come strictly in order: anything else could move the cursor past unsent entries.
    if (!parseNum(idx, i) || inFlight_.empty() || inFlight_.front().first != i) return fail("bad line", "bad");
    core_.acked(peer_, i);
    inFlight_.pop_front();
    if (nak) naks_++;
}

void NetSession::takeEntry(const std::string& idx, const std::string& rec) {
    uint32_t i;
    if (!parseNum(idx, i)) return fail("bad line", "bad");
    const char* reason = nullptr;  // null: ACK
    if (rec.empty()) {
        reason = "bad";
    } else if (rec[0] == (char)NetKind::Post) {
        NetPost p;
        if (!decode(rec, p)) {
            reason = "bad";
        } else {
            NetCore::Took t = core_.take(std::move(p), peer_);
            if (t == NetCore::Took::Stored) postsTaken_++;
            else if (t != NetCore::Took::Duplicate) reason = tookName(t);
        }
    } else if (rec[0] == (char)NetKind::Card) {
        NetCard c;
        if (!decode(rec, c)) reason = "bad";
        else if (!verifyCard(c)) reason = "Rejected";
        else core_.takeCard(c, peer_);  // an older card than ours is still handled
    } else if (rec[0] == (char)NetKind::Notice) {
        NetNotice nn;
        if (!decode(rec, nn)) reason = "bad";
        else core_.takeNotice(nn, peer_);  // a stale notice is still handled
    } else {
        reason = "bad";
    }
    if (reason) {
        naksSent_++;
        send("NAK " + idx + " " + reason);
    } else {
        send("ACK " + idx);
    }
}

// Snapshot records are not answered. One that does not decode is skipped: the line itself was
// well formed, and the record is the peer's data, checked again by NetCore anyway.
void NetSession::takeState(const std::string& rec) {
    if (rec.empty()) return;
    if (rec[0] == (char)NetKind::Card) {
        NetCard c;
        if (decode(rec, c)) core_.takeCard(c, peer_);
    } else if (rec[0] == (char)NetKind::Notice) {
        NetNotice nn;
        if (decode(rec, nn)) core_.takeNotice(nn, peer_);  // the rotate chain comes first, in order
    }
}

// History skips the ingress limit, so this side caps it: past kSyncHistoryMax H lines in one
// session the rest are read and dropped.
void NetSession::takeHistory(const std::string& rec) {
    if (histTaken_ >= kSyncHistoryMax) return;
    histTaken_++;
    NetPost p;
    if (rec.empty() || rec[0] != (char)NetKind::Post || !decode(rec, p)) return;
    if (core_.take(std::move(p), peer_, true) == NetCore::Took::Stored) postsTaken_++;
}

void NetSession::tick(uint32_t nowMs, bool heapOk) {
    if (failed_ || done()) return;
    if (!started_) {
        started_ = true;
        startMs_ = lastRxMs_ = nowMs;
    }
    if (heard_) {
        heard_ = false;
        lastRxMs_ = nowMs;
    }
    if ((uint32_t)(nowMs - startMs_) > kSyncDeadlineMs) return fail("too long", "timeout");
    if ((uint32_t)(nowMs - lastRxMs_) > kSyncTimeoutMs) return fail("timeout", "timeout");
    if (outOverflow()) return;
    if (phase_ != Phase::Sync || !heapOk) return;

    // 0. Whether to ask for history: only a board whose Sysop Net areas are empty does. Decided
    // here, with heap to spare, and always before this side's SEND.
    if (!wantDecided_) {
        wantDecided_ = true;
        if (!core_.hasNetPosts()) {
            send("WANT " + std::to_string(kSyncHistoryDays));
            sentWant_ = true;
        }
    }
    if (!peerSeq_) return;

    size_t budget = kSyncPerTick;
    bool streamed = false;  // S or H lines went out: unanswered, so the peer may rightly be quiet

    // 1. The snapshot, then SEND.
    if (!sentSend_) {
        if (sendSnap_ && !snapLoaded_) {
            snap_ = core_.state().snapshot();
            snapLoaded_ = true;
            snapPos_ = 0;
        }
        while (snapPos_ < snap_.size() && budget && roomForData()) {
            if (fits(snap_[snapPos_])) {
                send("S " + snap_[snapPos_]);
                budget--;
                streamed = true;
            }
            std::string().swap(snap_[snapPos_]);
            snapPos_++;
        }
        if (snapPos_ < snap_.size()) {
            if (streamed) lastRxMs_ = nowMs;
            return;
        }
        std::vector<std::string>().swap(snap_);
        send("SEND");
        sentSend_ = true;
    }

    // 2. History, if the peer asked for it, then HEND.
    if (peerWant_ && !histDone_) {
        uint32_t now = core_.nowUnix();
        uint64_t span = (uint64_t)histDays_ * 86400;
        uint32_t since = now > kNetClockSet && now > span ? (uint32_t)(now - span) : 0;
        std::string l;
        while (budget && roomForData()) {
            if (!core_.historyNext(hist_, since, l)) {
                hist_ = HistoryCursor();
                send("HEND");
                histDone_ = true;
                break;
            }
            if (!fits(l)) continue;
            send("H " + l);
            postsSent_++;
            budget--;
            streamed = true;
        }
        if (streamed) lastRxMs_ = nowMs;
        if (!histDone_) return;
    }

    // 3. Outbound log entries. Nothing new is fetched after DONE (the next session takes them), so
    // a peer that has both DONEs is never left with lines it will not answer.
    if (sentDone_) return;
    bool nothingPending = false;
    if (fetched_.empty() && inFlight_.empty()) {
        // The cursor only moves on acks, so fetching with lines in flight would fetch them again.
        auto got = core_.pendingFor(peer_, kSyncInFlight);
        nothingPending = got.empty();
        for (auto& e : got) fetched_.emplace_back(e.first, std::move(e.second));
    }
    while (budget && !fetched_.empty() && inFlight_.size() < kSyncInFlight && roomForData()) {
        auto& front = fetched_.front();
        if (!fits(front.second)) {
            // Too big for any receiver: it can never go. Passed over once everything before it
            // is answered, so the cursor never skips an entry still in flight.
            if (!inFlight_.empty()) break;
            core_.acked(peer_, front.first);
            fetched_.pop_front();
            continue;
        }
        auto e = std::move(front);
        fetched_.pop_front();
        bool post = e.second[0] == (char)NetKind::Post;
        send("E " + std::to_string(e.first) + " " + e.second);
        inFlight_.emplace_back(e.first, post);
        if (post) postsSent_++;
        budget--;
    }

    // 4. DONE: everything sent and answered, the peer's snapshot (and so any WANT) is in.
    if (nothingPending && peerSend_ && fetched_.empty() && inFlight_.empty()) {
        send("DONE");
        sentDone_ = true;
    }
    outOverflow();
}

}  // namespace axis::net
