// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// NetSession: one board to board sync over a byte stream. The two boards prove who they are,
// swap the net state, pass a new board its history and pass each other their outbound log
// entries. A pure state machine: the host feeds it received bytes, sends what takeOut returns
// and calls tick; it owns no socket and reads no clock of its own.
//
// The handshake: the dialer proves who it is first, and the answerer signs nothing until that
// proof checks out, so a stranger cannot use the answerer to sign for it. Both proofs sign one
// transcript binding the role, both nonces and both board ids. A dialer also checks that the
// answerer is the board it meant to dial (expectedBoard), so a card whose host points somewhere
// else gets nowhere. Plain TCP has no channel binding: with an expected board a relay needs a
// live on-path attacker, the accepted residual (the same as for everything that is signed only).
// On first contact with the first peer, before its board id is known (empty expectedBoard, trust
// on first use), a remote relay is still possible that once.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "net/areas.h"
#include "net/record.h"
#include "net/sign.h"

namespace axis::net {

constexpr size_t kSyncPerTick = 4;                    // data lines (S, H, E) per tick at most
constexpr size_t kSyncInFlight = 8;                   // E lines sent and not yet answered
constexpr size_t kSyncMaxLine = kNetMaxFrame + 32;    // a line longer than this is never sent or taken
constexpr uint32_t kSyncTimeoutMs = 30000;            // no complete line for this long fails
constexpr uint32_t kSyncDeadlineMs = 10 * 60 * 1000;  // a whole session at most
constexpr uint32_t kSyncStrangerDeadlineMs = 2 * 60 * 1000;  // an inbound session from a board not yet a link peer
constexpr uint32_t kSyncMaxLines = 4000;              // lines received per session at most
constexpr uint32_t kSyncHistoryDays = 30;             // what a new board asks for
constexpr size_t kSyncOutHigh = 2048;                 // no new data lines while this much is unsent
constexpr size_t kSyncOutMax = 16 * 1024;             // more unsent than this: the peer is not reading
constexpr uint32_t kSyncHistoryMax = 800;             // H lines taken per session (4 areas x 200)
constexpr size_t kSyncCardsAhead = 64;                // origins whose card went ahead of their posts, remembered

class NetSession {
public:
    // me is the card this side presents and signer the key matching me.pub (the board's own key,
    // or Rev's daily key on Rev's board). nonceHex16 comes from the host's random source. A
    // dialer's peer goes into NetCore's outbound link pool, an answerer's into the inbound one.
    // expectedBoard (dialer only; an answerer ignores it): the board being dialed. The host passes
    // the card's board when dialing a card, and the first peer's board once it is known. Empty is
    // only for the first peer before its board id is known (trust on first use).
    NetSession(NetCore& core, const NetCard& me, const KeyPair& signer, const std::string& nonceHex16, bool dialer,
               const std::string& expectedBoard);

    ~NetSession();  // wipes its copy of the signing key
    NetSession(const NetSession&) = delete;
    NetSession& operator=(const NetSession&) = delete;

    void feed(const char* p, size_t n);  // bytes from the socket
    std::string takeOut();               // bytes for the socket (send them before closing)
    void tick(uint32_t nowMs, bool heapOk);

    bool done() const { return !failed_ && phase_ == Phase::Sync && sentDone_ && peerDone_; }
    bool failed() const { return failed_; }
    const std::string& peer() const { return peer_; }
    const char* why() const { return why_.c_str(); }
    bool proven() const { return phase_ == Phase::Sync; }  // the peer proved who it is (the handshake passed)
    // The peer was a trusted link peer (NetCore::trustedPeer) before this session started: captured at
    // its hello, before this session adds it to a pool. The host gives the address of an inbound peer
    // that was not one a cooldown after the session.
    bool wasTrusted() const { return trusted_; }
    // The peer was in a link pool at its hello (trusted or not); decides the snapshot on first contact.
    bool wasLinkPeer() const { return wasLink_; }
    // How long this session may run in all: kSyncDeadlineMs, except an answerer's with a peer that was not
    // trusted (kSyncStrangerDeadlineMs), so a stranger cannot hold the one inbound slot (and with it this
    // board's dialing) for long. A history pull (either side sent WANT) gets kSyncDeadlineMs whoever the
    // peer is, so a new board's first pull is not cut at 2 minutes.
    uint32_t deadlineMs() const { return deadline_; }

    uint32_t postsSent() const { return postsSent_; }    // posts written out as E or H lines
    uint32_t postsTaken() const { return postsTaken_; }  // posts from the peer stored here
    uint32_t naks() const { return naks_; }              // our E lines the peer refused
    uint32_t naksSent() const { return naksSent_; }      // the peer's E lines we refused

private:
    enum class Phase { Hello, Proof, Sync };
    void line(std::string& l);
    void syncLine(std::string& l);
    bool hello(const std::string& l);
    void proof(const std::string& l);
    std::string transcript(char role) const;
    void enterSync();
    void takeEntry(std::string_view idx, std::string_view rec, std::string& line);
    void takeState(std::string_view rec);
    void takeHistory(std::string_view rec, std::string& line);
    void ack(std::string_view idx, bool nak, std::string_view reason);
    bool cardAhead(const std::string& postLine);
    void countDone();
    void sendHello();
    void send(std::string_view l);
    void send(std::string_view prefix, std::string_view rec);
    void fail(const char* why, const char* bye);
    bool roomForData() const { return out_.size() < kSyncOutHigh; }
    bool outOverflow();

    NetCore& core_;
    NetCard me_;
    KeyPair signer_;
    std::string nonce_;
    bool dialer_;
    std::string expected_;  // dialer: the board it meant to dial ("" on first use)

    Phase phase_ = Phase::Hello;
    bool failed_ = false;
    std::string why_;
    std::string in_, out_;

    std::string peer_, peerNonce_;
    NetCard peerCard_;  // held from the hello to the proof only
    bool wasLink_ = false;  // a link peer at its hello (decides the snapshot on first contact)
    bool trusted_ = false;  // a trusted link peer at its hello (decides the deadline and the cooldown)
    bool counted_ = false;  // this session's completion went to NetCore::sessionDone
    uint32_t deadline_ = kSyncDeadlineMs;

    // Sync progress, this side.
    uint32_t mySeq_ = 0;
    bool wantDecided_ = false, sentWant_ = false;
    bool sendSnap_ = false, snapLoaded_ = false, sentSend_ = false;
    std::vector<std::string> snap_;
    size_t snapPos_ = 0;
    bool histDone_ = false;
    uint32_t histDays_ = 0;
    HistoryCursor hist_;
    std::deque<std::pair<uint32_t, std::string>> fetched_;
    // Log index, and whether a nocard NAK for it can be cured: a post whose origin's card the peer has
    // from the handshake or got ahead of it this session. Without one, nocard is final for that entry.
    std::deque<std::pair<uint32_t, bool>> inFlight_;
    std::vector<std::string> cardsSent_;               // origins whose card went ahead of their first post
    bool held_ = false;                                // a transient NAK came: nothing more this session
    bool sentDone_ = false;

    // Sync progress, the peer.
    bool peerSeq_ = false, peerWant_ = false, peerSend_ = false, peerHend_ = false, peerDone_ = false;

    bool started_ = false, heard_ = false;  // heard_: a complete line came in since the last tick
    uint32_t startMs_ = 0, lastRxMs_ = 0;
    uint32_t linesRx_ = 0;

    uint32_t histTaken_ = 0;  // H lines passed to take this session
    uint32_t postsSent_ = 0, postsTaken_ = 0, naks_ = 0, naksSent_ = 0;
};

}  // namespace axis::net
