# AXIS Sysop Net and User Net: design

Status: draft for Rev's review, 2026-11-11. Decisions marked **(Rev)** are his; **(Claude's call)** can be changed.

## Why

AXIS boards are islands today. Axis Net (`src/bbs/axisnet.*`) only keeps a directory of other boards and passes short greetings. Rev wants every AXIS board to be part of a living network, like FidoNet in its day, that works without anyone running a server and keeps working over LoRa radio (the LORA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM) when the internet is gone.

The first job is sysop support: Rev can announce updates, and sysops can report bugs and help each other. The second is a place for callers: chat, tech help, games, and a shared catalog of files.

## Two nets

**Sysop Net (Rev)**
- Every AXIS board joins by default. A sysop can turn it off in Network Setup.
- Sysops only, no callers.
- Areas: Support, Announcements, Update Notices, Bug Reports.
- Rev moderates.

**User Net (Rev)**
- A board opts in, and gets in by a sysop vote (Rev), see "Joining User Net".
- Callers read and post.
- Areas: General Chat, Tech Help, Games, Files. (Rev named the topics; the area names are Claude's call.)
- Each sysop picks which areas their board carries.

**Both nets**
- Each net's areas show as their own groups in the message menu ("Sysop Net", "User Net"), separate from the local bases.
- Every post carries an origin line (the board name) and its path (the boards it passed through).

## How posts travel (Rev: board to board, no server; Rev's own BBS may act as the hub)

**Peers and flooding**
- Each board keeps a list of peers and connects to them directly over the internet.
- A new post goes to every peer, and every peer passes it on to its own peers.
- Each post has a globally unique ID. A board drops any post it already has.

**Store and forward**
- A peer that is down gets its posts later from a per-peer queue.
- A board that cannot accept incoming connections still works: it dials out, and the sync runs both ways on that connection. (Claude's call)

**Rev's board as hub and backup**
- A new board's first peer is Rev's board.
- From Rev's board, the new board learns the other boards (the existing Axis Net cards, extended) and then talks to them directly.
- Rev's board also relays and holds posts for boards that cannot reach each other.
- The network keeps running when Rev's board is down. Boards that already know each other keep syncing.

**The first-peer address**
- The first-peer address is never written into the public repo. It is a setting.
- The install menu fills it in from an untracked local file. A sysop can also type it, or it can come inside an invite.
- (Claude's call: the repo rule is that Rev's public hostname is never committed.)

**Radio fallback**
- When the internet is down, a board syncs through its LFD3K-SM modem, found over the existing AXIS hookup (modem on the LAN, mDNS).
- Over radio, posts above a size limit wait for the internet. The limit is a sysop setting, starting at 2 KB (Claude's call).
- File listings and moderation notices always go over radio. They are small.

## Joining User Net: new board voting (Rev: "like the bbs nuv, for only sysops")

It mirrors the board's own NUV (`src/bbs/nuv.*`).

**Applying**
- A board's sysop fills in an application: board name, sysop, location, a short blurb.
- The board's net ID and MAC are added automatically, and the application is signed.
- It travels over Sysop Net.

**Voting**
- Only sysops of current User Net member boards vote, from a "New Board Voting" screen in the User Net menu.
- Each member board gets one yes or no vote, with an optional comment. A second vote from the same board replaces its first.
- Each vote is signed with the voting board's key.

**Counting**
- Every board tallies the signed votes itself, so all boards reach the same result with no server.
- Enough yes votes admit the board. Enough no votes turn it down.
- The counts are network settings that only Rev can change, carried in a signed notice. They start at 3 yes and 3 no, the BBS NUV defaults. (Claude's call)

**Rev's override and veto (Rev)**
- Rev can admit or deny any applicant at once, like the sysop's V on the board.
- Rev's veto beats any vote count.
- A veto can come after a board was voted in, and then it removes that board from User Net.

## The net record (Rev: logs of every vote, member board data, and who did what)

Every board keeps a permanent record on its SD card of everything it sees on the net:
- **Votes:** every application, every vote with its comment and voting board, every result, and every admit, deny or veto from Rev.
- **Member boards:** every board's card as it changes over time: name, net ID, MAC, sysop, location, link type, when it joined, and when it was banned or removed.
- **Actions:** who did what. Posts, holds, kills, bans, lifts, setting changes, local sysop actions, and Net File listings and fetches, each with the board ID and handle that did it.

**How the record is kept**
- Each entry keeps the signature it arrived with, so anyone can check it later and nobody can quietly edit it.
- The record is append only and never trimmed by retention. It rolls over into dated files on the SD card. (Claude's call)
- Followers have no card; the main keeps the cluster's record.
- The sysop can browse and search the record from Traffic and Logs.
- Rev's board keeps the most complete copy, because everything passes through it sooner or later.

**What this replaces, and what it protects**
- The vote replaces invite keys from member boards. (Claude's call)
- A banned board that makes a new key has to win a new vote.
- A turned-down board can apply again after a waiting period, 30 days to start. (Claude's call)

## Identity and bans (Rev: "ban a board from the net")

**Board identity**
- On first boot, each board makes its own signing key pair and keeps the private half in NVS.
- The board's net ID is a short fingerprint of its public key.
- Every post, listing and announcement a board sends is signed with that key. A board cannot be impersonated by its name or its IP.
- The board's MAC address travels with its card as information, so Rev can see which hardware it is.
- A MAC is never trusted for anything: it is self-reported and easy to fake.

**Rev's net key**
- Rev holds a net key pair. The private half stays on Rev's board and PC only, never in the repo.
- Its public half is built into the firmware. Only notices signed with it count network-wide.

**Bans (Rev)**
- Rev can ban a board network-wide by its net ID, with the MAC shown beside it.
- Every board drops the banned board's traffic and refuses its connections.
- A banned sysop who wipes the board and makes a new key has to win a new User Net vote.
- Sysop Net is open by default, so a re-keyed board could rejoin it. Rev can ban the new ID too.

## Moderation (Rev: both)

**Rev, network-wide (signed notices, obeyed everywhere):**
- Hold a post.
- Kill a post.
- Ban a board.
- Lift any of these.
- Review held posts.

**Each sysop, on their own board only (nothing spreads):**
- Hide a post.
- Drop an area.
- Block a board from reaching theirs.
- Keep a local caller from posting to the nets.

## Net Files (Rev)

**Listings travel, files stay put**
- An upload to a file area the sysop shares on User Net sends out a small listing: name, size, description, uploader, the board that has it, and a signature.
- Boards carrying that area show these entries in a "Net Files" list beside their own files, marked with the board each lives on.
- If the file is removed at its home board, a removal notice deletes the listing everywhere. (Claude's call)

**Fetching (Rev: the caller's board fetches and hands it over)**
- The caller's board connects to the board that has the file and streams it through to the caller with the normal download protocols.
- Nothing is stored on the way, so a follower board without an SD card can do it too. (Claude's call)

**Size warnings (Rev)**
- If any leg of the trip is radio (between the boards, or the caller's own link reported by the modem) and the file is over the size limit, the caller sees a time estimate and decides. Example: "This file is 340 KB. Over the radio link at 3906 bps that's about 12 minutes. Fetch it anyway? (Y/N)"
- The estimate uses the slowest leg at its current speed.
- The limit is a sysop setting, starting at 64 KB (Claude's call).
- A trip that is Wi-Fi or internet all the way gets no warning.

**The home board stays in control**
- It only serves files from areas its sysop shares.
- It caps how many outside fetches run at once, so its own callers are not slowed down.
- The download counts against the caller's limits on their own board, and as a download on the home board. (Claude's call)

## Sysop menus (from the LFD3K-SM design, section 11)

Each net gets the same set:
- **This Node:** name, net ID, MAC, origin line, contact.
- **Peers / Uplinks:** per peer, the areas carried, priority, and enabled.
- **Network Areas:** which areas this board carries.
- **Sync and Routing:** schedule, radio first or internet first, size limits, hold big posts for the internet.
- **Gateway Radios:** the LFD3K-SM link.
- **Traffic and Logs.**
- **Moderation:** local actions, and for Rev, the network-wide ones.

User Net also gets **New Board Voting:** pending applications, the votes and comments so far, and the vote. Rev's board adds admit and deny.

## Constraints

**Memory**
- Board 1 idles at about 88 to 91 KB free heap.
- Net sync must work in small pieces, with a heap floor like the cluster user sync (`kSyncHeapFloor`).
- Net sync must never cost a caller their session.
- It must not stack a second TLS session on top of No-IP's.

**Storage and the cluster**
- Only the main board has the SD card. Net bases, queues and listings live there.
- A follower reaches them through the main over the cluster link, as message and file areas already do (`RemoteMessageStore`, `remotefiles`).
- Only the main talks to other boards on the net. The cluster counts as one net node.

**Build option**
- The whole feature compiles out with one build option (`nets`), being designed in the build options session.

**The repo**
- The repo is public and GPL.
- Nothing from the closed LFD3K-SM code goes into it. AXIS only talks to the modem over its documented interfaces (AT commands over TCP, telnet option 90, mDNS).
- No private keys, Rev's hostname or the cluster key in the repo.

## Goals

- Rev can post an update notice once, and every AXIS board's sysop sees it.
- A caller on any User Net board can read and post in the shared areas, and download a file that lives on another board.
- The net keeps working with Rev's board off, and with the internet down (over radio, for small traffic).
- Rev can ban a board, and it stays out.

## Non-goals

- No central server, database or website.
- No FidoNet compatibility for now. A gateway could come later (msgbase.h already notes JAM/PKT).
- No ANSI world map in this project. It is the next one, and it rides on Sysop Net.
- No private messages between boards' users (netmail) in the first version. (Claude's call)

## Anti-goals (would be a failure even if it works)

- A board crashes or drops a caller because net sync ran out of memory.
- Someone can post as another board, forge Rev's moderation, or get back in after a ban by changing an IP or MAC.
- A caller starts a radio download without being told how long it will take.
- Rev's hostname or any private key ends up in the public repo.

## Transport security (Rev: signed only)

- Board-to-board links are plain TCP (and plain radio sessions).
- Every post, listing, vote, notice and fetch request is signed, so nothing can be faked or altered.
- Nothing is encrypted. Nothing on the nets is private, since posts reach every board anyway.
- The reason is memory: no second TLS session on board 1.

## Settled after the builder check (Claude's calls unless marked)

**Radio sessions**
- A radio sync is a dialed, point-to-point session, not a broadcast.
- The board asks its LFD3K-SM modem, over AT commands on TCP, to `ATDT` a peer board's radio station name. The two boards then run the same store-and-forward sync they use over the internet, at radio speed.
- A board with no internet finds radio neighbours from the modem's found list (`AT+FOUND?`, fed by beacons).
- The tier speed comes from the telnet option 90 reports.

**Who posts where**
- Announcements and Update Notices accept only posts signed with Rev's net key.
- Support and Bug Reports take any Sysop Net board.
- User Net areas take any member board.
- Every board enforces a per-board ingress limit: posts per day and bytes per day, with network-wide defaults Rev sets by signed notice. A rogue board cannot fill anyone's SD card before a ban lands.

**Ordering of Rev's notices**
- Every network-wide notice (hold, kill, ban, lift, vote counts, limits) carries a sequence number from Rev's key. Boards may have no reliable clock, so time is never used.
- For one target, the highest sequence wins.
- A board that joins or comes back after a long time is sent the current state: the ban list, held posts and settings. It is not sent the history.
- A held post still travels but stays hidden on every board until Rev releases it, so a release shows it at once.

**Net Files reachability**
- The caller's board signs its fetch request with its board key, naming the file and the local caller's handle.
- A home board that only dials out cannot be fetched from directly. Its fetches relay through Rev's board while that is up. Otherwise the caller sees "that board can't be reached right now".
- Every listing says how its home board is reached (internet, radio, or both), which feeds the time estimate.

**Retention**
- Each net area keeps its newest 500 posts or 90 days, whichever is less. Both are sysop settings per board.
- Listings live as long as the file.
- Dedupe IDs are kept for 120 days.
- A new board's first sync pulls the last 30 days.
- A full SD card stops accepting net posts first, before anything local.

**Keys**
- The firmware carries two of Rev's public keys: one in daily use, one kept offline as a backup.
- A notice signed by either one can replace the other. A lost or leaked daily key is rotated without a reflash.
- A board's identity lives in NVS, and normal reflashes keep it.
- A board whose NVS is wiped is a new board: new ID, and a new User Net vote.

## Left to the builder

- The wire format and port.
- The signature algorithm. Keep it small and GPL-compatible; mbedTLS in ESP-IDF already has ECDSA.
- Post ID scheme and dedupe window.
- Queue formats on the SD card.
- Sync schedule defaults.
- The menu art.
- How Axis Net cards are extended for net ID and MAC.
