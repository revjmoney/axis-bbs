// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The Sysop Net and User Net message areas and NetCore, each board's share of the net: it takes
// posts, cards, Rev's notices, User Net applications, votes and Net Files listings from peers, makes this
// board's own posts and listings, stores them in the message base and logs what to pass on for its link peers.
// NetPostingStore wraps the board's message store so posting into a net area goes out on the net
// and held or killed posts stay hidden.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "bbs/msgbase.h"
#include "bbs/users.h"
#include "net/members.h"
#include "net/record.h"
#include "net/outlog.h"
#include "net/sign.h"
#include "net/state.h"

namespace axis::net {

// A callable passed down a call and used only during it: a pointer to it and one call function, so a lambda costs no
// std::function (no manager, no heap copy of its captures).
template <typename Sig>
class FnRef;
template <typename R, typename... A>
class FnRef<R(A...)> {
public:
    template <typename F, typename = std::enable_if_t<!std::is_same<std::decay_t<F>, FnRef>::value>>
    FnRef(F&& f) : obj_((void*)&f), call_([](void* o, A... a) -> R { return (*(std::remove_reference_t<F>*)o)(a...); }) {}
    R operator()(A... a) const { return call_(obj_, a...); }

private:
    void* obj_;
    R (*call_)(void*, A...);
};

struct NetArea {
    std::string id, name;
    bool revOnly;
};

// The four Sysop Net areas, in menu order.
const std::vector<NetArea>& sysopNetAreas();
const NetArea* sysopNetArea(const std::string& id);  // nullptr when id is not one of them
// The four User Net areas, in menu order (none Rev-only).
const std::vector<NetArea>& userNetAreas();
bool isUserNetArea(const std::string& id);      // one of userNetAreas
const NetArea* netArea(const std::string& id);  // either family; nullptr when id is neither
bool isNetAreaId(const std::string& area);      // starts with "sn_" or "un_"
// The caller may post to the nets: an account, net posts allowed (permNets) and not a twit.
inline bool mayPostToNets(const bbs::User* u) { return u && u->permNets && !u->twit; }

// The post id held in a Message's net field (the whole line or the headers-only start). "" if none.
std::string netPostId(const std::string& net);
// The board's usual date text ("YYYY-MM-DD HH:MM", local time) for a unix time, and back (0 if not
// parsable).
std::string netDateText(uint32_t unixTime);
uint32_t netDateUnix(const std::string& text);

// Where a seq counter (this board's posts, or its card) goes on from at start. found: a saved value was
// read (saved). With none and the clock set (nowUnix above kNetClockSet), the clock: above any seq a
// board would have used, so a lost file never brings back a used one. Never below saved, never 0.
uint32_t netSeqStart(uint32_t saved, bool found, uint32_t nowUnix);

constexpr size_t kMaxOutboundPeers = 4;    // boards this board dials, plus the first peer
constexpr size_t kMaxInboundPeers = 4;     // boards that dialed us; the longest unsynced goes first
constexpr uint32_t kOutLogKeep = 5000;     // outbound log entries kept
constexpr uint32_t kOutLogSlack = 500;     // the log is trimmed back to kOutLogKeep past this many more
constexpr uint32_t kNetKeepPosts = 500;
constexpr uint32_t kNetKeepDays = 90;
constexpr size_t kNetPendingBytes = 12 * 1024;  // pendingFor stops once its lines reach this much
constexpr uint32_t kNetClockSet = 1700000000;   // below this the board's clock is not set
constexpr uint8_t kNetReadStrikes = 3;          // pendingFor calls stuck on an unreadable post before it is gone
constexpr size_t kNetHistoryPerArea = 200;      // posts per area a new board's history pull gets at most
constexpr size_t kNetUserHistoryPerArea = 100;  // posts per User Net area a User Net pull gets at most
constexpr uint8_t kNetNakStrikes = 3;           // sessions in a row a transient NAK may hold an entry
constexpr uint8_t kNetTrustSessions = 2;        // completed sessions before an inbound-only link peer is trusted

// Net Files: each listing or tombstone (a removal) is one message in the hidden area kNetFilesArea, never in a
// caller's area list (isNetAreaId is false for it). Listings are gated on the User Net area kNetFilesUserArea.
constexpr const char* kNetFilesArea = "nf_list";
constexpr const char* kNetFilesUserArea = "un_files";
constexpr uint32_t kNetListingsMax = 500;       // live listings held, at intake and by the trim (the host sets 100 without an SD card)
constexpr uint32_t kNetListingsPerBoard = 200;  // live listings from one home board
constexpr uint32_t kNetGoneKeepDays = 120;      // a tombstone is kept this long (with the clock set)
constexpr uint32_t kNetGoneKeepCount = 200;     // without a clock, the newest this many tombstones are kept
constexpr uint32_t kNetIndexStep = 32;          // listings a rebuild of the listing index takes per call
constexpr uint32_t kNetListingDropsPerSession = 8;  // listings from peers that may replace or remove one held, per session
constexpr uint32_t kNetListingDropsPerDay = 64;     // the same, per day across every session (uptime days without a clock)
constexpr uint32_t kNetFilesHeldPulls = 3;          // listings pulls in a row that may end held before fhistory is done anyway

// The words a receiver puts after NAK <index>. Permanent ones move the sender's cursor past the entry;
// transient ones (nocard, store) do not: the sender sends nothing more that session and tries the
// entry again next time. Any other word (an older board's) counts as permanent.
constexpr const char* kNakBad = "bad";          // not a valid record, a bad path, not a net area
constexpr const char* kNakDate = "date";        // dated outside the keep window (with a clock)
constexpr const char* kNakBanned = "banned";    // its board is banned
constexpr const char* kNakRejected = "rejected";  // refused by this board's own block list
constexpr const char* kNakSig = "sig";          // the signature does not match the card held here
constexpr const char* kNakRevOnly = "revonly";  // a Rev-only area, and not from Rev's board
constexpr const char* kNakKilled = "killed";    // killed by Rev
constexpr const char* kNakLimited = "limited";  // over the origin's daily ingress limit
constexpr const char* kNakNoCard = "nocard";    // transient: the origin's card is not held here
constexpr const char* kNakStore = "store";      // transient: the message base did not take it
constexpr const char* kNakArea = "area";        // a User Net area this board does not carry, or it is no member
constexpr const char* kNakMember = "member";    // a User Net post from a board that is no member by the tally here
bool netNakTransient(std::string_view word);

// Where a history walk is: the posts to send, merged across the walked areas oldest first, as
// (area index << 28 | message number), and how many went. Only these numbers are kept (at most
// kNetHistoryPerArea per Sysop Net area, kNetUserHistoryPerArea per User Net area, 4 bytes each),
// never whole messages. A session runs one walk at a time, with one cursor.
struct HistoryCursor {
    bool loaded = false;  // picks holds the walk
    size_t pos = 0;
    std::vector<uint32_t> picks;
};

// `store` must be the board's real message store, never the NetPostingStore wrapping it (that
// would loop through originate and hide held posts from trim and sending).
//
// Everything to pass on goes into the outbound log as one entry, "<kind> <key> <exclude>":
//   P <area> <msgnum> <postId> <exclude>   a post, read back from the message base
//   C <board> <exclude>                    the board's current card
//   N <group> <target> <exclude>           the notice stored now for that target
//   N R <seq> <exclude>                    a rotate notice from the chain
//   A <board> <exclude>                    the board's User Net application held now
//   V <app> <voter> <exclude>              the voter's vote on that applicant held now
//   L <msgnum> <id> <exclude>              a listing or tombstone, read back from kNetFilesArea
// Applications and votes are read back from the apps file (setAppsFile), never held whole in RAM.
// exclude is a comma list of boards not to send it to, or "-". Each link peer reads the log from
// its own cursor; RAM stays small whatever the number of posts or boards.
class NetCore {
public:
    enum class Took { Stored, Duplicate, Rejected, Banned, Limited };

    NetCore(bbs::MessageStore& store, const KeyPair& me, std::function<uint32_t()> nowUnix, OutLog& log);
    ~NetCore();  // wipes its copy of the key
    NetCore(const NetCore&) = delete;
    NetCore& operator=(const NetCore&) = delete;

    NetState& state() { return state_; }
    const NetState& state() const { return state_; }
    NetSeen& seen() { return seen_; }
    const NetSeen& seen() const { return seen_; }
    const std::string& board() const { return myBoard_; }
    bool isRevBoard(const std::string& board) const;  // its card's pub is one of Rev's keys
    // The User Net tally (applications, votes and Rev's member notices).
    NetMembers& members() { return members_; }
    const NetMembers& members() const { return members_; }
    // The User Net areas this board carries (ids from userNetAreas; others are dropped). Empty: User Net off.
    void setUserAreas(std::vector<std::string> carried);
    const std::vector<std::string>& userAreas() const { return userAreas_; }
    bool carries(const std::string& area) const;
    // This board is a User Net member by its own tally.
    bool selfMember() const { return members_.status(myBoard_) == NetMembers::Status::Member; }
    // /net/apps.txt: append adds one line and gives the byte offset it starts at (false on failure);
    // scan calls each on the file's lines in order until it returns false (for the host's load and
    // compaction); readAt reads the one line starting at a byte offset. The file is never read whole.
    // NetCore keeps each held record's offset in members() and reads a record back only when RAM says it
    // is held.
    using AppsLineFn = std::function<bool(const std::string& line)>;
    void setAppsFile(std::function<bool(const std::string& line, uint32_t& off)> append,
                     std::function<void(const AppsLineFn& each)> scan,
                     std::function<bool(uint32_t off, std::string& line)> readAt) {
        appsAppend_ = std::move(append);
        appsScan_ = std::move(scan);
        appsReadAt_ = std::move(readAt);
    }
    const std::function<void(const AppsLineFn& each)>& appsScan() const { return appsScan_; }

    // A post from a peer (fromPeer may be empty). Stores it and logs it to pass on. Taken by value
    // so the caller can move it in and the body is held once. With the clock set, a post dated
    // more than keepDays ago or more than a day ahead is Rejected. A User Net post is refused (area)
    // unless this board carries its area and is a member, and (member) unless its board is a member.
    // history: a post from a new board's history pull; it skips the ingress limit (the session
    // caps how many it takes), every other check still runs.
    // nak, if given, gets the NAK word for a refusal (kNak*), or nullptr when Stored or Duplicate.
    Took take(NetPost p, const std::string& fromPeer, bool history = false, const char** nak = nullptr);
    // A card or notice from a peer. True if it changed the state (then it is logged to pass on).
    bool takeCard(const NetCard& c, const std::string& fromPeer);
    bool takeNotice(const NetNotice& n, const std::string& fromPeer);
    // A User Net application or vote, from a peer or made here (fromPeer ""). Verified and counted by
    // members(); when it changed something its line is appended to the apps file, logged to pass on and
    // noted (then "member <board> admitted|denied|removed" when the board it names changed status).
    // True if it changed something.
    bool takeApp(const NetApp& a, const std::string& fromPeer);
    bool takeVote(const NetVote& v, const std::string& fromPeer);
    // This board's own card and Rev's notices made here; both already signed by the caller.
    bool setOwnCard(const NetCard& c);
    bool originateNotice(const NetNotice& n);
    // Builds, signs, stores and logs a post from this board. Returns its id, or "" on failure.
    // num, if given, gets the stored message number. A User Net area only while this board is a
    // member and carries it.
    std::string originate(const std::string& area, const bbs::Message& m, int* num = nullptr);

    // Net Files listings. Each is one message in kNetFilesArea: subject the name, from the uploader, to the home
    // board, date as for posts, no body, net the encoded line. One record is kept per (board, area, name): a listing
    // or tombstone (gone) drops every record of its name with a lower seq, and one that comes later at or below the
    // seq held for that name is Duplicate.
    // A listing from a peer, or made here (fromPeer ""). Same order of checks as take(NetPost):
    // valid, banned, blocked, card and signature, this board carries un_files and is a member, the home
    // board is a member (NAK area / member), killed, dedupe, tombstone, ingress (line size), store.
    // This board's own listings skip the ingress limit.
    Took takeListing(const NetListing& l, const std::string& fromPeer, bool history = false, const char** nak = nullptr);
    // This board's own listing for a file in a shared area, or its removal (gone). Signs with this board's
    // key and the next post seq. Returns the id, or "" when this board is not a member carrying un_files.
    std::string originateListing(NetListing l);
    // Live listings, newest first, from skip on, at most max; each gives the stored line. Skips held, killed,
    // banned-home and tombstoned ones. Returns the next skip, or -1 at the end.
    int listingsPage(int skip, int max, FnRef<void(const NetListing&)> each);
    bool findListing(const std::string& id, NetListing& out);  // a live one only
    // Whether this board holds its own live listing of (area, name), shown or not (held, killed or hidden here), by
    // the index. The host sends a removal or a rename's removal only for such a name (an upload's temp name or a ZIP
    // its rezip replaced never was listed). False while the index is not ready.
    bool listedHere(const std::string& area, const std::string& name);
    // For a User Net pull: the stored lines from pos on (0 to start; the store's resume point, a byte offset in the
    // file store), live listings (tombs false; not held, killed or banned-home) or tombstones younger than
    // kNetGoneKeepDays (tombs true; all of them without a clock), added to out until they reach kNetPendingBytes.
    // pos is left where the next batch starts. True once the scan reached the end. Nothing while this board is not
    // a member carrying un_files.
    bool listingLines(uint32_t& pos, bool tombs, std::vector<std::string>& out);
    // /net/files.idx, the listing index: a 44 byte head (IdxHead), then one 24 byte record per listing or tombstone
    // in kNetFilesArea, in store order (IdxRec). read and write work on byte ranges of the file (a write at or past
    // its end grows it); the host gives the file, a test a buffer. random fills bytes from the host's random source
    // (a test stubs it). takeListing and findListing look things up in it and read just the records it points at
    // (MessageStore::readAt), never scanning the area. Its hashes are keyed BLAKE2b (32 bits kept) with a 128 bit
    // random key drawn at each build and kept in the head, so names that collide cannot be chosen ahead; the key
    // comes from no signing key. A head that is not clean (a reset during a change), a tail that does not match the
    // area (a sysop's move into it, or a replaced file), or a failed read or write sends it to be built again from
    // the area, kNetIndexStep listings a call to listingIndexStep; until then listings are refused with the
    // transient "store" NAK and findListing finds nothing. After a drop from the area it is built again the same way (the
    // offsets moved).
    void setListingIndex(std::function<bool(uint32_t off, void* p, size_t n)> read,
                         std::function<bool(uint32_t off, const void* p, size_t n)> write,
                         std::function<void(void* p, size_t n)> random) {
        idxRead_ = std::move(read);
        idxWrite_ = std::move(write);
        idxRandom_ = std::move(random);
        idx_ = Idx::Unknown;
    }
    // A session starts: the budget of listings from peers that may replace or remove a held one (each a rewrite of
    // the area) is full again. Past it, or past the daily budget (kNetListingDropsPerDay, which a session start does
    // not reset), such a listing is refused with the transient "store", before ingress and before it is stored.
    void listingSessionStart() { listingDrops_ = 0; }
    // One bounded piece of index work (a look at the head, or a piece of a rebuild); the host calls it from its idle
    // tick. True while more is to do.
    bool listingIndexStep();
    bool listingIndexReady() const { return idx_ == Idx::Ready; }
    // A listing line from a peer waits, left unread, while the index is being built: true then. Each call does one
    // more piece of that work (or the first look at the head), so a session waiting on it shortens the wait. False
    // once it is ready, and false from a failed index write until it is ready again (takeListing then refuses with
    // "store", as before), so a line waits on one rebuild at most and an index that cannot be built never holds it.
    bool listingIndexWait();
    // The index file may have changed or gone under the host (a card swap, a serial file command, a format): what is
    // held of it in RAM is forgotten, so the next look checks its head and the area's tail again.
    void listingIndexForget() {
        idx_ = Idx::Unknown;
        idxFailed_ = false;
    }
    // The caps the daily trim holds live listings to (kNetListingsMax and kNetListingsPerBoard by default).
    void setListingCaps(uint32_t total, uint32_t perBoard) {
        listingsMax_ = total;
        listingsPerBoard_ = perBoard;
    }

    // A board we sync with, in one of two pools. Outbound (kMaxOutboundPeers): boards this board
    // dials, and the first peer; only the host's dialing adds to it. Inbound (kMaxInboundPeers):
    // boards that dialed us and proved who they are. A full pool loses the one in it synced
    // longest ago, never the first peer; an add never evicts from the other pool, so strangers
    // dialing in can never push out the boards this board dials. A board already held is kept
    // (an outbound add moves an inbound one to the outbound pool). A new one starts at the log's
    // end; it gets the current state from the snapshot exchange. Calling it again marks a sync now.
    void addLinkPeer(const std::string& board, bool outbound);
    // The board's first peer: added as an outbound link peer and pinned.
    void setFirstPeer(const std::string& board);
    bool isLinkPeer(const std::string& board) const { return links_.count(board) > 0; }
    // A link peer this board dials (outbound pool), or an inbound one with kNetTrustSessions completed
    // sessions. Anything else is a stranger: its inbound session gets the short deadline and its address
    // the cooldown, so a board cannot escape both just by being added to the inbound pool once.
    bool trustedPeer(const std::string& board) const;
    // A session with this link peer completed (both sides sent DONE).
    void sessionDone(const std::string& board);
    // Up to max (log index, encoded line) pairs for a link peer, oldest first, stopping once the
    // lines reach kNetPendingBytes (at least one is returned if any is ready). Entries excluded for
    // it or confirmed gone (killed, banned origin, card or notice no longer held, post not in the
    // area) are skipped. A read failure stops the list there; it is tried again next time.
    // peerUserNet false (a peer whose card lacks net2): applications, votes and User Net posts are
    // skipped with no read, since such a peer would only refuse them. peerNet3 false (no net3): listings are.
    std::vector<std::pair<uint32_t, std::string>> pendingFor(const std::string& peer, size_t max, bool peerUserNet = true,
                                                             bool peerNet3 = true);
    // The peer has the entry at index: its cursor moves past it (acks come in order).
    void acked(const std::string& peer, uint32_t index);
    // The peer answered the entry at index with a transient NAK: the cursor stays, unless that entry
    // has now held it kNetNakStrikes sessions in a row (the sender cannot fix it, say it lacks the
    // origin's card too), then it moves past. True if the cursor moved.
    bool nakedTransient(const std::string& peer, uint32_t index);
    // "<peer> <index> <last sync unix> <out|in> <completed sessions>" per line, "history <0|1>" once
    // whether the history pull is done is known, "uhistory <0|1>" (the User Net pull) and "fhistory <0|1>" (the
    // listings pull), and "fheld <n>" while that pull is open and has ended held n times in a row (filesHistoryHeld).
    // On load the pool field is optional: without it a
    // peer goes to the outbound pool while it has room, else the inbound one. Without the sessions
    // field (an older file) a peer counts as trusted: it was a link peer before this build.
    std::string saveCursors() const;
    void loadCursors(const std::string& text);

    // History for a new board: the stored posts of every Sysop Net area dated at or after
    // sinceUnix (0 for all), the newest kNetHistoryPerArea per area, merged across the areas in
    // date order (oldest first; ties by seq), one encoded line per call. In date order each origin's
    // posts arrive about in seq order, so the receiver's seen window holds them whatever their area.
    // Held, killed and banned-origin posts are skipped. False once the walk is over.
    bool historyNext(HistoryCursor& c, uint32_t sinceUnix, std::string& line);
    // True if any Sysop Net area holds a post (stops at the first one found).
    bool hasNetPosts() const;
    // Whether a session should ask for history (WANT): until a pull has finished (historyGot). With no
    // flag saved yet (a fresh board, or one upgraded from a build without it) it is decided once here:
    // areas holding posts count as done, empty ones as not done. Reads headers then; call it with heap.
    bool needHistory();
    // A session got HEND after its WANT, with at least one H line: the pull is done for good.
    void historyGot() { history_ = 1; }
    // -1: not known yet (nothing saved), 0: not done, 1: done.
    int historyState() const { return history_; }

    // The User Net pull (UWANT, U, UEND), its own flag: asked until a pull reaches UEND having taken or
    // already held at least one U line (an empty UEND, from anyone, leaves it to the next). Not done on a
    // board with nothing saved, so a board upgraded from v0.53 asks once. The host resets it when this
    // board becomes a member or starts carrying another area.
    bool needUserHistory() const { return !userHistory_; }
    void userHistoryGot() { userHistory_ = true; }
    void resetUserHistory() {  // the listings pull too
        userHistory_ = filesHistory_ = false;
        filesHeld_ = 0;
    }
    // The listings pull (asked with UWANT of a net3 peer while this board is a member carrying un_files), its own
    // flag, so a board upgraded from Phase 2 with uhistory done still gets the listings held before: done when a
    // pull reached "UEND F" (the peer sent every listing it holds) with none refused "store" and the session's
    // listing cap not reached (NetSession; either one counts as held). Saved with the cursors as "fhistory".
    bool needFilesHistory() const { return !filesHistory_; }
    void filesHistoryGot() {
        filesHistory_ = true;
        filesHeld_ = 0;
    }
    // A pull reached "UEND F" with a listing refused "store" (say past a drop budget) or at the session's listing
    // cap: counted, saved with the cursors as "fheld <n>"; the kNetFilesHeldPulls-th in a row marks the pull done
    // anyway, so it cannot repeat forever.
    void filesHistoryHeld() {
        if (++filesHeld_ >= kNetFilesHeldPulls) filesHistoryGot();
    }
    // The apps file line at off, if it is a record held now (members().holds). For a User Net pull, which
    // walks members().offsets().
    bool userRecord(uint32_t off, std::string& line);
    // For a User Net pull: like historyNext, over the User Net areas this board carries whose bit is set
    // in areaMask (bit i: userNetAreas()[i]), the newest kNetUserHistoryPerArea per area.
    bool userHistoryNext(HistoryCursor& c, uint32_t sinceUnix, uint8_t areaMask, std::string& line);
    // The clock NetCore was given (unix time); below kNetClockSet it is not set.
    uint32_t nowUnix() const { return now_(); }

    // Log appends that failed (the entry is then never passed on), for the host to report.
    uint32_t appendFailures() const { return appendFailures_; }
    // Applications and votes taken whose line could not be added to the apps file (held, never passed on).
    uint32_t appsFailures() const { return appsFailures_; }

    // The net record (net/netrecord.h): called for every take result, card or notice applied and post made here.
    // who is the peer it came from ("" for this board), what says what happened, signedLine is the record's encoded
    // line (only for posts stored or made, cards and notices; "" otherwise).
    using EventFn = std::function<void(const std::string& who, const std::string& what, const std::string& signedLine)>;
    void setOnEvent(EventFn f) { onEvent_ = std::move(f); }
    // This board's own moderation (never flooded): a blocked board's posts and listings are Rejected and its sessions
    // refused; a hidden post is not shown here (NetPostingStore) but is still passed on. A blocked board's listings
    // held from before and hidden listings are not shown, found or sent in a pull (shown). The host calls
    // moderationChanged when either list changes (the count of live listings is taken again).
    void setLocalFilter(std::function<bool(const std::string& board)> blocked, std::function<bool(const std::string& postId)> hidden) {
        blocked_ = std::move(blocked);
        hidden_ = std::move(hidden);
        liveCount_ = -1;
    }
    bool blockedHere(const std::string& board) const { return blocked_ && blocked_(board); }
    bool hiddenHere(const std::string& postId) const { return hidden_ && hidden_(postId); }
    void moderationChanged() { liveCount_ = -1; }

    // The seq the next post from this board takes.
    uint32_t postSeq() const { return seq_; }
    void setPostSeq(uint32_t s) { seq_ = s ? s : 1; }
    // Saves the seq after the one a post takes, before the post is stored or logged: reserve(next) must
    // keep next durably (the board writes /netseq.txt in internal flash). False refuses the post, so a
    // reset can skip a seq but never hand one out twice.
    void setSeqReserve(std::function<bool(uint32_t next)> reserve) { reserve_ = std::move(reserve); }

    void setRetention(uint32_t keepPosts, uint32_t keepDays) {
        keepPosts_ = keepPosts;
        keepDays_ = keepDays;
    }
    // Once a day at most: retention for every Sysop Net and User Net area, pruning of the state and the seen
    // set, trimming the outbound log, and for Net Files: killed listings, banned boards' live ones and old tombstones dropped, then the
    // caps (a home board past its cap loses its own lowest seqs; past the total, the board holding the most
    // does; never this board's own, never a tombstone). Without a clock (nowUnix not past kNetClockSet) it runs
    // whenever called, by count only: the host calls it once per 24 hours of uptime.
    void trim(uint32_t nowUnix);

private:
    struct Link {
        uint32_t cursor = 0;
        uint32_t lastSync = 0;
        uint32_t stuckAt = 0xFFFFFFFFu;  // the entry whose post could not be read last time
        uint8_t strikes = 0;             // calls in a row stuck on it
        uint32_t nakAt = 0xFFFFFFFFu;    // the entry the peer last answered with a transient NAK
        uint8_t nakStrikes = 0;          // sessions in a row it did
        bool outbound = false;           // which pool it is in
        uint8_t sessions = 0;            // completed sessions with it (capped at 255)
    };
    size_t poolSize(bool outbound) const;
    bool makeRoom(bool outbound, const std::string& keep);  // evicts within one pool

    uint32_t day() const { return now_() / 86400; }
    void syncLimits() { ingress_.setLimits(state_.postsPerDay(), state_.bytesPerDay()); }
    void log(const std::string& entry);
    enum class Res { Ready, Skip, Retry };  // send it; excluded or gone for good; try again later
    int storePost(NetPost& p, std::string& line);
    // A refusal: what a take gives back, the record's why and the NAK word.
    struct Refusal {
        Took t;
        const char* why;
        const char* word;
    };
    // The checks a post and a listing share. Origin: its board banned, blocked here, its card held and the record
    // signed by that card's key (signedBy). User: this board carries the User Net area and is a member, and the
    // board is a member by the tally. True on a refusal, with r set.
    bool refusedOrigin(const std::string& board, FnRef<bool(const std::string& pub)> signedBy,
                       Refusal& r) const;
    bool refusedUser(const std::string& area, const std::string& board, Refusal& r) const;
    // Every listing and tombstone in kNetFilesArea by a header scan (which gives a listing's line whole), oldest
    // first; lines that do not decode are passed over. each returns false to stop.
    void eachListing(FnRef<bool(const bbs::Message& m, const NetListing& l)> each);
    bool shown(const NetListing& l) const;  // live, not held, killed or hidden here, its board not banned or blocked here
    void trimListings(uint32_t nowDay, bool clock);
    bool listingDropAllowed();  // both drop budgets, this session's and this day's, have room
    // The listing index (setListingIndex). key: the keyed hash of "<board>|<area>|<name>", board: the keyed hash of
    // the board id, flags 1 for a tombstone, at where the message starts in the area.
    struct IdxRec {
        uint32_t key, board, seq, num, flags, at;
    };
    // magic "NFX3", clean (1) or not, records, the hash key, and the area's last message (any, junk too): whether
    // there is one, its number, where it starts, and where the area ends after it (to tell the area changed behind
    // the index's back).
    struct IdxHead {
        uint32_t magic, clean, count, key[4], hasLast, lastNum, lastAt, end;
    };
    enum class Idx : uint8_t { Unknown, Rebuild, Ready };
    bool idxFailed_ = false;  // an index write failed since it was last ready: listingIndexWait waits no more
    uint32_t idxHash(const std::string& s) const;
    uint32_t idxKey(const std::string& board, const std::string& area, const std::string& name) const;  // a record's key
    IdxRec idxRec(const NetListing& l, const bbs::Message& m) const;
    bool idxReady();  // looks at the head if not known yet
    bool idxHead(bool clean);
    bool idxEach(FnRef<bool(const IdxRec&)> each);  // false if a read failed
    // The first live record of that key (key 0: of that board key and seq) that reads back and passes ok, into out.
    bool idxLive(uint32_t key, uint32_t board, uint32_t seq, FnRef<bool(const NetListing&)> ok, NetListing& out);
    bool idxTailOk();
    void idxStart();      // a rebuild from the start, with a new key, kNetIndexStep listings a listingIndexStep
    Res resolve(const std::string& entry, const std::string& peer, std::string& line, bool peerUserNet, bool peerNet3);
    Res readRecord(bool held, uint32_t off, std::string& line);
    // Who is a member and who is denied, before a change, to note what the change did (empty without a
    // record callback).
    struct Roll {
        std::vector<std::string> in, denied;
    };
    Roll roll() const;
    void noteRoll(const std::string& who, const Roll& before);
    bool appended(const std::string& line);
    bool walk(HistoryCursor& c, uint32_t sinceUnix, const std::vector<const NetArea*>& areas, size_t perArea,
              std::string& line);
    bool clockSet() const { return now_() > kNetClockSet; }
    void dropPost(const std::string& id);
    bool stored(const std::string& area, const std::string& id);  // the post is in that area (a header scan)
    void trimArea(const std::string& area, uint32_t nowDay, bool clock);

    bbs::MessageStore& store_;
    KeyPair me_;
    std::function<uint32_t()> now_;
    OutLog& log_;
    std::string myBoard_;
    NetState state_;
    NetMembers members_;  // after state_: it reads the state
    NetSeen seen_;
    Ingress ingress_;
    uint32_t seq_ = 1;
    std::function<bool(uint32_t)> reserve_;
    uint32_t keepPosts_ = kNetKeepPosts, keepDays_ = kNetKeepDays;
    uint32_t listingsMax_ = kNetListingsMax, listingsPerBoard_ = kNetListingsPerBoard;
    std::function<bool(uint32_t, void*, size_t)> idxRead_;
    std::function<bool(uint32_t, const void*, size_t)> idxWrite_;
    Idx idx_ = Idx::Unknown;
    IdxHead hd_ = {};
    uint32_t idxPos_ = 0;  // where a rebuild goes on in the area
    std::function<void(void*, size_t)> idxRandom_;
    uint8_t listingDrops_ = 0;  // this session's (listingSessionStart)
    // Live listings in kNetFilesArea for listingsPage, -1 to count again (every post or drop there sets it), taken at
    // state gen liveGen_ (a notice can change what is shown).
    int      liveCount_ = -1;
    uint32_t liveGen_ = 0;
    uint8_t listingDropsToday_ = 0;  // this day's, across sessions (listingDropAllowed)
    uint32_t listingDropsDay_ = 0;   // the day it counts (day(), with the clock set; trim resets it without one)
    uint32_t lastTrimDay_ = 0xFFFFFFFFu;
    std::map<std::string, Link> links_;
    std::string firstPeer_;
    int8_t history_ = -1;  // see historyState
    bool userHistory_ = false;  // a User Net pull reached UEND
    bool filesHistory_ = false;  // a listings pull reached UEND F
    uint8_t filesHeld_ = 0;      // listings pulls in a row that ended held (filesHistoryHeld)
    std::vector<std::string> userAreas_;
    std::function<bool(const std::string&, uint32_t&)> appsAppend_;
    std::function<void(const AppsLineFn&)> appsScan_;
    std::function<bool(uint32_t, std::string&)> appsReadAt_;
    uint32_t appendFailures_ = 0, appsFailures_ = 0;
    EventFn onEvent_;
    std::function<bool(const std::string&)> blocked_, hidden_;
};

// The board's message store with the net on top: a post into an sn_ or un_ area goes through
// NetCore::originate, and held, killed or locally hidden posts in those areas are hidden. Everything else passes through.
class NetPostingStore : public bbs::MessageStore {
public:
    NetPostingStore(bbs::MessageStore& inner, NetCore& core) : inner_(inner), core_(core) {}
    std::vector<bbs::Message> headers(const std::string& area) override;
    bool read(const std::string& area, int num, bbs::Message& out) override;
    int post(const std::string& area, const bbs::Message& m) override;
    int drop(const std::string& area, const std::function<bool(const bbs::Message&)>& gone) override;

private:
    bool hidden(const bbs::Message& m) const;
    bbs::MessageStore& inner_;
    NetCore& core_;
};

}  // namespace axis::net
