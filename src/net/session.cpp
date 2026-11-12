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
// Before the first E post of an origin board in a session, the sender puts that board's card on an
// S line of its own (if it holds it), so the receiver has the card to check the post with. Only card
// records may come as S lines after SEND. A NAK reason is one word (kNak* in areas.h): "nocard" and
// "store" are transient and stop the sender for the session without moving its cursor, except a
// "nocard" for a post whose origin's card the sender does not hold either: no session can cure that,
// so the cursor moves past it at once.
// BYE <reason> ends the session at any point.
#include "net/session.h"

#include <algorithm>
#include <cstring>

namespace axis::net {

namespace {

bool isHex(std::string_view s, size_t n) {
    if (s.size() != n) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

// Digits only, no leading zero (except "0" itself), fits in 32 bits.
bool parseNum(std::string_view s, uint32_t& out) {
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

// Splits "VERB rest" at the first space, as views into l (nothing copied). rest is empty when there
// is no space.
void verbOf(std::string_view l, std::string_view& verb, std::string_view& rest) {
    size_t sp = l.find(' ');
    verb = l.substr(0, sp);
    rest = sp == std::string_view::npos ? std::string_view() : l.substr(sp + 1);
}

// A record this side may put on the wire: with any prefix (at most 32 characters) it stays within
// what a receiver takes.
bool fits(const std::string& rec) { return !rec.empty() && rec.size() <= kNetMaxFrame; }

}  // namespace

NetSession::NetSession(NetCore& core, const NetCard& me, const KeyPair& signer, const std::string& nonceHex16,
                       bool dialer, const std::string& expectedBoard)
    : core_(core), me_(me), signer_(signer), nonce_(nonceHex16), dialer_(dialer),
      expected_(dialer ? expectedBoard : std::string()), deadline_(dialer ? kSyncDeadlineMs : kSyncStrangerDeadlineMs) {
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

NetSession::~NetSession() { wipe(signer_); }

void NetSession::send(std::string_view l) {
    out_ += l;
    out_ += '\n';
}

// A data line from a prefix and a record, appended straight to the output (no copy of the record).
void NetSession::send(std::string_view prefix, std::string_view rec) {
    out_ += prefix;
    out_ += rec;
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
    std::vector<std::string>().swap(cardsSent_);
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
        if (in_.capacity() < in_.size() + seg)  // grows by doubling, but never past one line's worth
            in_.reserve(std::min(kSyncMaxLine, std::max(in_.size() + seg, 2 * in_.capacity())));
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
            countDone();
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
    if (core_.blockedHere(c.board)) {  // blocked by this board's sysop (the host gives no back-off for it)
        fail("blocked", "bad");  // the wire says only "bad"; "blocked" stays in why()
        return false;
    }
    peer_ = c.board;
    // Who it was before this session: enterSync adds it to a pool, so this is the last moment to tell.
    wasLink_ = core_.isLinkPeer(peer_);
    trusted_ = core_.trustedPeer(peer_);
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
    // Proven. Whether it was a link peer before (taken at its hello) decides the snapshot on first
    // contact; adding it first pins its card so the card is never evicted for room. A board we dialed
    // goes into the outbound pool, one that dialed us into the inbound pool.
    if (trusted_) deadline_ = kSyncDeadlineMs;
    core_.addLinkPeer(peer_, dialer_);
    core_.takeCard(peerCard_, peer_);
    peerCard_ = NetCard();
    phase_ = Phase::Sync;
    mySeq_ = core_.state().highSeq();
    send("SEQ " + std::to_string(mySeq_));
    // WANT (or not) follows in the first tick with heap to spare: hasNetPosts reads headers.
}

// The line is parsed in place: verb, index and record are views into l. A data line's record is
// decoded straight from it, and l is let go before the record is stored (see takeEntry).
void NetSession::syncLine(std::string& l) {
    std::string_view verb, rest;
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
        std::string_view idx = rest, reason;
        if (verb == "NAK") verbOf(rest, idx, reason);
        return ack(idx, verb == "NAK", reason);
    }
    if (verb == "E") {
        std::string_view idx, rec;
        verbOf(rest, idx, rec);
        peerDone_ = false;  // more after a DONE: a new DONE follows
        return takeEntry(idx, rec, l);
    }
    if (verb == "S") {
        // After SEND only a card, sent ahead of its board's posts.
        if (peerSend_ && (rest.empty() || rest[0] != (char)NetKind::Card)) return fail("bad line", "bad");
        return takeState(rest);
    }
    if (verb == "H") {
        if (!sentWant_ || peerHend_) return fail("bad line", "bad");  // history only when asked for
        return takeHistory(rest, l);
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

void NetSession::ack(std::string_view idx, bool nak, std::string_view reason) {
    uint32_t i;
    // Answers come strictly in order: anything else could move the cursor past unsent entries.
    if (!parseNum(idx, i) || inFlight_.empty() || inFlight_.front().first != i) return fail("bad line", "bad");
    bool curable = inFlight_.front().second;
    inFlight_.pop_front();
    if (nak) naks_++;
    if (held_) return;  // an earlier transient NAK holds the cursor: later answers must not move it past
    // nocard for a post whose origin's card this side lacks too: no later session cures it, so it is final.
    if (nak && netNakTransient(reason) && (curable || reason != kNakNoCard)) {
        // The peer may take it later (its card arrives, its store recovers): nothing more goes this
        // session and the cursor stays on it, unless it has held it too many sessions in a row.
        held_ = true;
        std::deque<std::pair<uint32_t, std::string>>().swap(fetched_);
        core_.nakedTransient(peer_, i);
        return;
    }
    core_.acked(peer_, i);
}

// Before an origin's first post this session: its card on an S line, so the receiver can check the
// post. Not for this board or the peer (the handshake gave those), nor a card already sent. True when
// the peer has the origin's card from this session (so a nocard NAK may yet be cured), false when this
// side has no card to give.
bool NetSession::cardAhead(const std::string& postLine) {
    std::string origin = netPostId(postLine).substr(0, 16);
    if (origin.size() != 16) return false;
    if (origin == me_.board || origin == peer_) return true;
    if (std::find(cardsSent_.begin(), cardsSent_.end(), origin) != cardsSent_.end()) return true;
    const NetCard* c = core_.state().card(origin);
    if (!c) return false;
    std::string l = encode(*c);
    if (!fits(l)) return false;
    if (cardsSent_.size() < kSyncCardsAhead) cardsSent_.push_back(origin);
    send("S ", l);
    return true;
}

// A completed session (both DONEs) counts once towards trusting an inbound-only link peer.
void NetSession::countDone() {
    if (counted_ || !done()) return;
    counted_ = true;
    core_.sessionDone(peer_);
}

// rec is a view into line. A post is decoded from it, then line is let go before the post is stored,
// so the line and the stored copies are never all held at once.
void NetSession::takeEntry(std::string_view idx, std::string_view rec, std::string& line) {
    uint32_t i;
    if (!parseNum(idx, i)) return fail("bad line", "bad");
    const char* reason = nullptr;  // null: ACK
    if (rec.empty()) {
        reason = kNakBad;
    } else if (rec[0] == (char)NetKind::Post) {
        NetPost p;
        if (!decode(rec, p)) {
            reason = kNakBad;
        } else {
            std::string().swap(line);  // idx and rec are no longer looked at
            if (core_.take(std::move(p), peer_, false, &reason) == NetCore::Took::Stored) postsTaken_++;
        }
    } else if (rec[0] == (char)NetKind::Card) {
        NetCard c;
        if (!decode(rec, c) || !verifyCard(c)) reason = kNakBad;
        else core_.takeCard(c, peer_);  // an older card than ours is still handled
    } else if (rec[0] == (char)NetKind::Notice) {
        NetNotice nn;
        if (!decode(rec, nn)) reason = kNakBad;
        else core_.takeNotice(nn, peer_);  // a stale notice is still handled
    } else {
        reason = kNakBad;
    }
    std::string num = std::to_string(i);  // the index as it came (parseNum takes one spelling only)
    if (reason) {
        naksSent_++;
        send("NAK " + num + " " + reason);
    } else {
        send("ACK " + num);
    }
}

// Snapshot records are not answered. One that does not decode is skipped: the line itself was
// well formed, and the record is the peer's data, checked again by NetCore anyway.
void NetSession::takeState(std::string_view rec) {
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
void NetSession::takeHistory(std::string_view rec, std::string& line) {
    if (histTaken_ >= kSyncHistoryMax) return;
    histTaken_++;
    NetPost p;
    if (rec.empty() || rec[0] != (char)NetKind::Post || !decode(rec, p)) return;
    std::string().swap(line);  // rec is no longer looked at
    if (core_.take(std::move(p), peer_, true) == NetCore::Took::Stored) postsTaken_++;
}

void NetSession::tick(uint32_t nowMs, bool heapOk) {
    if (failed_ || done()) return;
    if (!started_) {
        started_ = true;
        startMs_ = lastRxMs_ = nowMs;
    }
    if (heard_ || !heapOk) {
        // Short of heap the host reads nothing (TCP holds the peer back), so the peer's silence is
        // ours: the silence timer stands still. The whole session's deadline still runs.
        heard_ = false;
        lastRxMs_ = nowMs;
    }
    if ((uint32_t)(nowMs - startMs_) > deadline_) return fail("too long", "timeout");
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
                send("S ", snap_[snapPos_]);
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
            send("H ", l);
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
    bool nothingPending = held_;  // held by a transient NAK: nothing more this session
    if (!held_ && fetched_.empty() && inFlight_.empty()) {
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
        bool curable = post && cardAhead(e.second);
        send("E " + std::to_string(e.first) + " ", e.second);
        inFlight_.emplace_back(e.first, curable);
        if (post) postsSent_++;
        budget--;
    }

    // 4. DONE: everything sent and answered, the peer's snapshot (and so any WANT) is in.
    if (nothingPending && peerSend_ && fetched_.empty() && inFlight_.empty()) {
        send("DONE");
        sentDone_ = true;
        countDone();
    }
    outOverflow();
}

}  // namespace axis::net
