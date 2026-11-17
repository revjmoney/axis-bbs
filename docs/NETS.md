<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Rev. J. Money -->
# Sysop Net and User Net

## What it is

Sysop Net is a small network of AXIS boards run by sysops, for sysops. Each board that has it turned on adds four Sysop Net message areas for the sysop. Posts spread between boards by dialing: a board calls its first peer on a timer, trades what each side is missing, and learns about more peers from the call. Every post and every board card is signed with the poster board's key, so a board can tell who made a thing. Nothing is encrypted.

Only the main board of a cluster runs it. A follower forwards the @NET commands to the main.

## Settings

Set them with `@SET name=value` over serial, the sysop menu, or the setup pages.

| Setting | Meaning |
| --- | --- |
| `net_sysop` | On unless set to 0. Set 0 to leave the net off. |
| `net_first_peer` | The board this one dials first: `host:port` or `host:port@<16 hex board id>`. Blank means none. |
| `net_sync_min` | Minutes between dials. Default 15. |
| `net_keep_posts` | Posts kept per area. Default 500. |
| `net_keep_days` | Days a post is kept. Default 90. |
| `net_name` | The name on this board's net card. Blank uses `bbs_name`. |
| `net_sysop_name` | The contact on the card. Blank uses `sysop_handle`. |
| `net_location` | The location on the card. |
| `net_user` | Off unless 1. Set 1 for this board to apply to User Net and carry its areas once it is a member. |
| `net_user_areas` | The User Net areas this board carries, a comma list of `un_chat`, `un_tech`, `un_games`, `un_files`. Default all four. Rev's board always carries all four. |
| `net_user_level` | The lowest security level that may read and post in the User Net areas. Default 20. |
| `net_files_areas` | The file areas shared on User Net (see "Net Files"). Blank shares nothing. |
| `net_files_radio_kb` | KB over which a caller on a radio link is asked before a Net Files fetch. Default 64. |

The `@board` part of `net_first_peer` is the first peer's board id, 16 hex characters (shown by `@NET` on that board). With it set, a call to that address must prove it is that board or the call is dropped. Without it, this board trusts whichever board answers the first time and remembers that board id for later calls. The value is never shown in clear on screen; the sysop pages say `set (hidden)`.

## Commands

Type them over serial, in the sysop menu, or in the PC panel.

Status:

- `@NET` shows the board id, the net state and counts. Example: `@NET`
- `@NET SYNC` dials the first peer soon instead of waiting. Example: `@NET SYNC`
- `@NET PEERS` lists known peers and when each was last reached. Example: `@NET PEERS`
- `@NET RECORD [n [skip]]` shows the newest n lines of the net record (default 20, most 100), newest first. The optional second number skips that many of the newest lines first, so you can page back through older ones. Examples: `@NET RECORD 50`, and `@NET RECORD 20 20` for the 20 lines before the newest 20
- `@NET LOCAL` lists what this board has hidden or blocked.
- `@NET FILES [skip]`, `@NET LISTALL <area>` and `@NET FETCHTEST <id>` are for Net Files (see "Net Files" below).
- `@NET RECORD FIND <text> [n [skip]]` searches the net record for text. `@NET RECORD KIND votes|members|actions|all [n [skip]]` lists one kind. `@NET RECORD MONTHS` lists the record files. `@NET RECORD SHOW <index>` or `@NET RECORD SHOW <file>:<offset>:<time>` shows one line field by field and whether its signature checks. See "The record browser" below.

User Net (see "User Net" below):

- `@NETAPPLY [blurb]` applies to join User Net. Example: `@NETAPPLY A small BBS for old computers`
- `@NETVOTE <board id> Y|N [comment]` votes on an applicant (members only). Example: `@NETVOTE 0123456789abcdef Y good board`
- `@NET APPS` lists the applications held, with the counts and your own vote. `@NET MEMBERS` lists the members. `@NET WAITING` lists the applicants this board has not voted on. `@NET APP <board id>` shows one application, its counts and each member's vote with the comment.
- On Rev's board only: `@NETADMIT <board id>`, `@NETDENY <board id>`, `@NETVETO <board id> YES` and `@NETVOTES yes=<1-20>,no=<1-20>`.

Local moderation, which only changes this board and is never sent anywhere:

- `@NETHIDE <post id>` hides a post here. Example: `@NETHIDE 3f9a1c2b`
- `@NETUNHIDE <post id>` shows it again.
- `@NETBLOCK <board id>` drops a board's calls and posts here. Example: `@NETBLOCK 0123456789abcdef`
- `@NETUNBLOCK <board id>` lifts the block.

Net-wide moderation. These work only on a board whose signer is one of Rev's keys, and the answer is a notice every board checks against Rev's public keys:

- `@NETBAN <board id>` bans a board.
- `@NETHOLD <post id>` holds a post for review.
- `@NETKILL <post id> YES` drops a post on every board. Without `YES` it only explains what it would do.
- `@NETLIFT <board id or post id>` lifts a ban or a hold.
- `@NETLIMITS posts=<n>,bytes=<n>` sets the net-wide post and size limits. Example: `@NETLIMITS posts=200,bytes=65536`
- `@NETROTATE <64 hex public key> YES` replaces the other one of Rev's two keys net-wide. Serial only.

Serial only, never from telnet or the panel:

- `@NETKEY SET <64 hex seed>` loads Rev's seed on this board.
- `@NETKEY CLEAR` removes it.

## The Sysop Net page

Open it from System, then Sysop Net, on the sysop menu. It shows the status, this node (board id, MAC, origin, and the editable name, contact and location), the peers, and sync and routing (sync interval, keep posts, keep days, first peer, and a Sync now row). Traffic and logs shows the newest record lines. Moderation lists what is hidden or blocked here and asks for an id to hide, unhide, block or unblock. On Rev's board only, rows for the net-wide ban, hold, kill, lift and limits appear as well. The first row turns the whole net on or off.

## User Net

User Net is the second net, for callers. Sysop Net is for sysops only; User Net carries four areas that callers read and post in: `un_chat` (General Chat), `un_tech` (Tech Help), `un_games` (Games) and `un_files` (Files). A board gets in by a vote of the boards already in, and Rev can admit, deny or veto.

### How to apply

Turn it on with `@SET net_user=1`, choose the areas with `net_user_areas` and the lowest caller level with `net_user_level`, then:

```
@NETAPPLY <blurb>
```

The application carries the board's id, its public key, the card's name, sysop and location, the board's MAC, your blurb (up to 240 printable characters; line breaks are dropped) and a number counting that board's applications. It is signed with the board's own key and travels over Sysop Net. `@NETAPPLY` is refused when `net_user` is off, when the board is already a member or has already applied, when it is vetoed, while its counters wait for the clock (a board whose `/netseq.txt` was lost waits for a set clock to put their floor above any number used before), and for 30 days after a deny: 30 days from the day Rev's deny notice was applied on this board, or, for a board turned down by the vote count with no deny from Rev, 30 days from the signed date of its own application. Until then `@NETAPPLY` says in how many days it may apply again. A reply of `@OK applied` means it was taken here and will be passed on.

### How to vote

Only member boards vote. List who is waiting with `@NET WAITING`, read one with `@NET APP <board id>`, and vote:

```
@NETVOTE <board id> Y|N [comment]
```

The comment is up to 72 characters. A board cannot vote on its own application. A second vote on the same application replaces the first. Each vote is signed with the voting board's own key and checks by itself, so no card is needed to verify it.

### The counts

Every board tallies the signed records itself, so all boards reach the same result with no server. Only votes from boards that are members by that same tally count. By default 3 yes votes admit an applicant and 3 no votes deny it. Rev can change the two counts, each 1 to 20, with `@NETVOTES yes=<n>,no=<n>`. Only the latest application of a board counts, and a new application starts a new tally. At most 16 applications and 256 member votes are held.

When the 16 are full, a new application takes the place of the oldest one that no longer matters to the tally: one Rev has admitted, denied or vetoed, or one from a banned board (a ban fixes its standing). A lift does not bring an evicted application back; the board applies again, or a pull brings it. With no such application the new one is refused. A board holds at most 16 votes from any one voter on applications it does not hold (a board with no application here, or a number other than the one held), not counting votes on applications Rev has already decided, so a member cannot fill the pool with votes on made-up applications. When the 256 member votes are full, votes on decided applications make room first, then the votes of boards that are no longer members (vetoed or banned): they stop counting at once and move to the small pool of outsider votes, as they would at the next restart.

### Rev's admit, deny and veto

On Rev's board:

- `@NETADMIT <board id>` admits the applicant at once.
- `@NETDENY <board id>` denies it at once. The board may apply again after 30 days.

`@NETADMIT` and `@NETDENY` name the application they decide, so they need the board's application held on Rev's board; without one they are refused.
- `@NETVETO <board id> YES` keeps it out for good, and beats any vote count. A veto can come after a board is in, and then it leaves User Net. Without `YES` the command only explains what it would do.

Each is a signed notice every board checks against Rev's public keys. Every board holds up to 64 such notices; past that a notice for a new board is refused, never an old one dropped, since the tally depends on every one. Only Rev's own commands and the auto seal's admits make them, so an outsider cannot use up the 64.

### Auto seal

When the count on a board admits an applicant, Rev's board signs the matching admit notice itself (four a pass at most), so the result is fixed for every board and does not depend on who voted later. A deny by the count is not signed: every board already reaches it from the same votes, and signing one would spend one of the 64 notices on each board turned down. A board turned down by the count may apply again 30 days after its own application's date; Rev denies by hand with `@NETDENY` when he wants the wait to run from his deny instead. Rev's own boards and boards Rev admitted are always members.

### Carrying the areas

A member board with `net_user` on carries the areas in `net_user_areas`. Callers at `net_user_level` or above see them in the message menu, each area name starting with `User Net: `. A caller whose net posts permission is off (`@PERM <handle> nets off`) cannot post to either net, and neither can a twit. A board that is not a member, or does not carry an area, refuses its posts. A board that becomes a member, or starts carrying another area, asks its peers for the User Net pull again (see below).

### Syncing User Net

Boards that have User Net say `net2` in the links field of their card. Only a peer whose card has `net2` is sent User Net records and the User Net pull, so an older board never sees them. A new member, or a board with a new area, asks with `UWANT`; the peer answers with the applications and votes it holds as `U` lines, then the newest posts of the areas asked for, then `UEND`. The pull is asked for on each call until one reaches `UEND` having brought at least one `U` line this board took or already held, as for the history pull. An empty answer (from a stranger, a fresh board, or a peer that answers `UEND` at once) leaves it to the next call, so the applications and votes held before a link was made still arrive. Applications and votes are passed on to every net2 peer whether or not this board is a member.

### Two more NAK words

A refused User Net post is answered with one of these (both permanent, never sent again):

| Word | Meaning |
| --- | --- |
| `area` | A User Net area the receiver does not carry, or the receiver is not a member itself. |
| `member` | The post's board is not a member by the receiver's tally. |

### The record browser

`@NET RECORD` (see Commands) shows the newest lines of the net record. The browser adds searching and kinds:

- `@NET RECORD FIND <text> [n [skip]]` lists lines holding the text.
- `@NET RECORD KIND votes|members|actions|all [n [skip]]` lists applications and votes, member changes, or the actions.
- `@NET RECORD MONTHS` lists the record files (one per month with an SD card).
- `@NET RECORD SHOW <index>` or `@NET RECORD SHOW <file>:<offset>:<time>` shows one line field by field, and `check` says whether its signature holds. The listings print the `<file>:<offset>:<time>` form, which names the very line even as the record grows.

Listings are newest first, at most 100 lines, with `older from <index>` when more are left. A search looks at the newest 2000 lines at a time. The sysop menu's Traffic and logs page uses the same browser.

### /net/apps.txt

The signed application and vote lines are kept whole in `/net/apps.txt` on the storage root, one line each, appended as they are taken. The board holds only compact fields in RAM and reads a line back by its byte offset, so the file is never read whole at run time. It is read two lines a pass at boot. Past twice the size of what is held (and 64 KB), the file is rewritten with only the lines still held, streamed through a temp file; the old file steps aside as `apps.old` until the new one is in place, and a restart after a cut puts the right one back.

## Net Files

Net Files lets a caller on one board download a file that lives on another board. A board lists some of its file areas on User Net. Every member board that carries `un_files` holds those listings, and its callers browse them in the Files menu under **Net Files (User Net)**. A download goes from the home board (the board that has the file) through the caller's board to the caller. Nothing is stored on the way. Net Files needs User Net: a board lists and fetches only while it is a member that carries `un_files`.

### Sharing

Pick the file areas to share with `net_files_areas`, a comma list of file area ids. Blank shares nothing. Example: `@SET net_files_areas=freeware,gcode`. The sysop menu has the same choice under System, User Net, Net Files: each file area is a row that flips between shared and not shared. The page also sets the radio size limit. The count of listed files is on the `files:` line of `@NET` (the Status row).

| Setting | Meaning |
| --- | --- |
| `net_files_areas` | The file area ids shared on User Net, a comma list. Blank shares nothing. |
| `net_files_radio_kb` | A caller on a radio link is asked before a fetch of a file over this many KB. Default 64. 0 asks for any file that has something in it. |

- A new upload into a shared area is listed once it is stored, with the uploader's handle. Removing or renaming a file in a shared area sends the removal (a rename also lists the new name).
- Files that were in the area before you shared it are not listed on their own. Use `@NET LISTALL <area>` once to list them.
- A listing is signed with this board's key. It names the file, its size and CRC-32, the area, a description (up to 120 characters), the uploader, and that the board can be reached over the internet.
- A row for a board whose card has no host reads `(can't be reached)` in place of its description.

### Commands

- `@NET FILES [skip]` lists the live listings this board holds, 20 a page, one a line: `<id> <home board name> <area> <name> <size>`. When more are left it ends with `more with @NET FILES <n>`. Example: `@NET FILES 20`
- `@NET LISTALL <area>` lists every file already in a shared area that this board has not listed. The area must be in `net_files_areas`. It works through the area a few files at a time, so the board stays responsive. Names this board already lists are skipped.
- `@NET FETCHTEST <id>` is for the bench. It fetches a listing to the board itself, reads it through as a caller's sender would, and puts the result in the event log: `@OK <bytes> crc ok`, `@OK <size> crc bad` or `@ERR <word>`. It runs across loop turns and never blocks the board. With no id it shows the last result. Example: `@NET FETCHTEST 0123456789abcdef-12`
- `@NET` has a line `files: <n> listed, <m> shared areas`. The count includes listings that are held or killed.

### Caps and limits

| What | Limit |
| --- | --- |
| Live listings held | 1000 (100 on a board with no SD card) |
| Live listings from one home board | 200 (100 on a board with no SD card). This board's own listings are not held to it. |
| A removal (tombstone) is kept | 120 days (200 removals on a board with no clock) |
| Listings from peers that replace or remove one already held | 8 a session and 64 a day |
| A description | 120 characters |

The caps are applied once a day, when the net trims, so between trims a board can hold a few more. A listing past the drop budget is refused with the `store` NAK and comes again on a later call. That keeps a peer from making this board rewrite its listing file over and over. One listing is kept for each name: a newer listing of the same name replaces an older one, and a removal beats any older listing of its name, whichever arrives first. A board's own listings are never held back by the budget.

Listings live in a hidden message area (`nf_list`) that no caller sees, one message each. They travel on the same calls as posts, but only to a peer whose card says `net3` in its links. A board that becomes a member (or starts to carry `un_files`) asks its peers for the listings they hold, as it does for applications and votes.

### The listing index: /net/files.idx

Finding a listing by name would mean reading the whole listing area. So the board keeps `/net/files.idx` on the storage root: a small head, then one fixed record for each listing and each removal, holding a keyed hash of its home board, area and name, its seq and where its message is. A lookup reads this file and then only the one message it points at. The hash key is random, made when the index is built and kept in its head, so nobody can choose names that collide.

The index is rebuilt from the listing area when it is missing, when its head says a change was cut short (a reset), when its tail does not match the area, or when a read or write fails. It is also rebuilt after listings are dropped. A missing or unclean index is rebuilt a few listings at a time in the board's idle time. After listings are dropped it is rebuilt at once. While it is being built, listings from peers are refused with `store` (they come again on a later call), nothing new is listed here, and a fetch call is answered `NO busy`. It is safe to delete the file.

### The fetch call

A fetch is its own call to the home board's telnet port. It opens with `AXISFILE 1` where a sync opens with `AXISNET 2`, and it uses no sync slot. A board serves one fetch call at a time and has one fetch of its own running at a time.

```
dialer: AXISFILE 1 <dialer nonce>
server: HI <server board> <server nonce> <signature>
dialer: GET <listing id, the caller's handle, offset, signature>
server: OK <bytes from the offset to the end>
        then the raw bytes, and the home board closes
```

- `HI` is signed with the home board's key over both nonces. The dialer checks it against the home board's card before it sends `GET`.
- `GET` is signed with the calling board's key. The home board checks that signature against the calling board's card. It checks that the listing is live and its own, that its area is still shared, that the calling board is a member and neither banned nor blocked, and that the file's size is what was listed.
- A refusal is `NO <word>` and the call ends. The words are `busy` (the serve slot is taken, the board is short of memory, its index is being rebuilt, this board is not serving at all (net off, still loading, or a follower), or the dialer's address is backed off after failed calls), `gone` (no such live listing, or the file is missing), `size` (the file changed after it was listed), `member` (the calling board is not a member), `banned` (the calling board is banned or blocked) and `sig` (the signature does not check). A request that does not parse is `bad`.
- A call that moves nothing for 20 seconds ends. While the bytes stream, the caller's board sends a one-byte keepalive every 10 seconds, so a slow radio caller (TCP held back at the far end) is not taken for a dead dialer.
- The caller's board checks the listing's CRC-32 as the bytes arrive, and before the last block goes to the caller's ZMODEM or XMODEM sender. A file that does not match fails the call with `crc`, so the caller never gets the end of a changed file. A `crc` failure is not retried.
- A sender may ask to go back (a ZMODEM re-position). The board keeps an 8 KB window; if the sender goes behind it, the board makes a new call at the new offset and keeps checking the same CRC. That is done at most 3 times for one download, then the fetch ends `retries`.
- A caller's program that resumes its own partial file (an offset past the start) cannot be checked. That download is **unverified**: it is allowed, and `unverified (resumed)` is written to the event log and the net record.
- A home board that trickles (under about 1 KB in 2 minutes while the caller's side has room) is cut off `slow`. A fetch that nobody asks about for 60 seconds before its transfer starts is let go.

When a fetch is served, the home board writes `fetch served <id> to <handle>@<board>` to the net record and the event log. A refused or stopped call goes to the event log only.

### What a caller sees

In Files, the last row of the area list is **Net Files (User Net)**. It shows for callers who may open `un_files`. The list is 15 a page, with the name, size, home board name and description. A caller picks a file and the board runs the normal download gate (the same checks and refusals as for a local file). Then:

1. A caller on a radio link, for a file over `net_files_radio_kb`, is asked: `This file is <n> KB. Over the radio link at <bps> bps that's about <m> minutes. Fetch it anyway? (Y/N)`. If the modem has not reported its rate, the rate of the caller's LoRa tier is used, or the slowest tier's.
2. The caller picks ZMODEM or XMODEM.
3. The board says `Connecting to <home>...`. When the home board answers, the transfer starts.

The messages: `that board is busy, try again in a minute` (busy), `that file is no longer there` (gone or size), and `that board can't be reached right now` for everything else (no host, no answer, a failed call). A transfer that stops part way says `the file transfer from <home> stopped`.

### Strict area levels

A file this board lists itself is not fetched. It goes straight from the local area, and the area's own security level applies as for any local file: a caller below the area's lowest level is refused with `that file is in an area closed to your account`. The sysop passes. The same holds for a caller on a follower. A listing from another board has no local level (the home board decided), so it is not affected.

### Followers

A follower's callers get Net Files through the main board. The follower asks the main with four requests: `G <skip> <max>` for a page of the list, `b` to begin a fetch for a caller and to ask how it is going, `t` for a piece (up to 2 KB) of the file as it arrives, and `e` to let the fetch go. The ops are named `G`, `b`, `t` and `e` because `B`, `T` and `E` were already taken by other follower requests. The main has one fetch slot, so one caller in the whole cluster fetches at a time; a second hears `busy`. A follower that leaves gives up its fetch. A fetch that is ready is held through its transfer, and ends when the follower says `e`, leaves, or the transfer stalls or goes slow.

A file a caller uploads on a follower into a shared area is listed with that caller's handle as the uploader, once the main has the final file (a ZIP's repack included). An older follower that sends no handle gets the sysop's handle. A main that is older, or has nets off, gives a follower no Net Files row.

### Decisions

- **No relay yet.** A board whose card has no host cannot be fetched from. Relaying through Rev's board is Phase 4.
- **Old files are listed only by `@NET LISTALL`.** Turning sharing on does not list what is already there.
- **Fetches are logged, not counted.** A served fetch is a line in the net record. A listing has no download counter.
- **Followers are included**, as above.
- **Area levels are strict**, as above.
- A listing's reach is always internet in this phase.

## The PC test peer

`axis_netpeer` (built with the PC build) is a net node on the PC, for testing a board against something other than a board. Besides `post`, `peers`, `sync` and `list`, it speaks User Net with these commands, one per line on stdin:

- `user on|off` carries all four User Net areas (the peer has no per-area setting) and says `net2` on the peer's card, or stops. It is remembered in `user.txt`.
- `apply <blurb>` applies, as `@NETAPPLY` does (`\n` in the blurb is a line break). The peer needs `user on` first.
- `vote <board> y|n [comment]` votes, once the peer is a member.
- `apps` lists the applications held, with the counts. `members` lists the members.

The peer keeps the signed lines in `apps.txt` in its folder, and its application and vote counters in `appseq.txt` and `voteseq.txt`. It uses the board's own tally, so it counts exactly as a board does. It is never one of Rev's boards, so it does not seal.

The peer also speaks Net Files, with the same portable code the board uses (the listings in NetCore, the fetch server and the fetch client), so it works as the bench partner. Its card says `net3`. It needs `user on` and to be a member before it can list a file.

- `share <folder>` serves the files in a folder as the area `pc`. The folder is kept in `share.txt`. `share` alone says what is shared. Nothing is offered until a file is listed.
- `list <file> [desc]` lists a file of that folder (name, size, CRC-32 and the description). A first word that is an area name (`support`, `announce`, `updates`, `bugs`, or the `sn_` form such as `sn_support`) with nothing after it still lists that area's posts, as before.
- `unlist <file>` sends the removal of its listing.
- `files` lists the live listings it holds, its own and other boards'.
- `fetch <id> <outfile>` fetches a listing from its home board's card host and port, checks the CRC and writes the file. It writes `<outfile>.part` and renames it on success, so a failed fetch leaves no file and never touches an existing one.
- While it listens, it answers `AXISFILE 1` fetch calls for the files it listed, with the same checks and `NO` words as a board (signature, banned or blocked, member, this node carries `un_files` and is a member, the listing, the file, its size). The one difference: it answers `busy` only when a call is already running, not for an unready index or low memory. A dialing peer has no listener, so it only fetches.

Example:

```
user on
share ./pcfiles
list tool.nc a G-code test
files
fetch 0123456789abcdef-12 got.bin
```

It keeps the listing index in `files.idx` in its folder. It prints no MAC and no key material. Its card says `net2,net3` with `user on`, and plain `internet` with it off, so a peer with user off is not a Net Files peer. A fetch call is served without holding up the loop, so a slow caller does not stall a sync.

## Files

Under `/net/` on the storage root:

- `record/<yyyymm>.log` is the net record, one file per month (SD card). Without a card it is the single `record.log`.
- `outlog.txt` is the log of what this board still has to pass on.
- `local.txt` holds this board's own hide and block lines.
- `firstpeer.txt` remembers the first peer's address and the board id learned from it.
- `apps.txt` holds the signed User Net applications and votes (see "/net/apps.txt").
- `files.idx` is the Net Files listing index (see "The listing index"). It can be deleted and is rebuilt.

Also under `/net/`: `state.txt` (cards and Rev's notices), `seen.txt` (which posts this board has seen) and `cursors.txt` (how far each link peer has been sent, how many calls with it completed, and whether this board's history pull is done).

In internal flash:

- `/netkey.txt` is this board's own signing seed, made on first boot.
- `/netrev.key` is Rev's seed, written by `@NETKEY SET`. It wins over `/netkey.txt` when present.
- `/netseq.txt` holds the next post number this board will use, its card's number, and the next vote and application numbers, beside the key. It lives in internal flash so a board that boots without its SD card keeps counting where it left off; peers drop a post whose number they have already seen. The new number is written before a post is stored or sent, so a reset can skip a number but never use one twice. If the file is missing and the clock is set, all four numbers start from the current time (seconds since 1970), which is above any number used before. Older boards kept these as `/net/postseq.txt` and `/net/card.txt`; those are read once as a floor.

The key files are never shown by `@TYPE` or the other serial file commands, in any spelling of the name. A board never sends its seed anywhere.

## Rev's keys

Rev makes two keys, a daily one and a backup, with the PC tool `axis_netkey` (built with the PC build, found in the build folder):

```
axis_netkey new local/netkeys/rev-daily.seed
axis_netkey new local/netkeys/rev-backup.seed
```

Each run writes a seed file and prints two lines: `pub <64 hex>` (the public key) and `board <16 hex>` (the board id that key gives). The seed is never printed. The tool refuses to overwrite a file or to write into a missing folder. `axis_netkey pub <file>` prints the same two lines for an existing seed.

Only the public keys go into the firmware, in `src/net/revkeys.cpp`. The seeds stay on Rev's PC. Keep the backup seed offline (a drive in a drawer, not on any board and not in git; `local/` is ignored by git).

To sign as Rev, load the daily seed on the board over serial:

```
@NETKEY SET <64 hex seed from rev-daily.seed>
```

That board then signs as Rev and can send net-wide notices. `@NETKEY CLEAR` goes back to the board's own key.

To rotate, sign in with the key that is still good, make a new seed with `axis_netkey new`, and send its public key:

```
@NETROTATE <new 64 hex public key> YES
```

This replaces the other one of Rev's two slots on every board. Then load the new seed the usual way. Because there are two slots, either can be replaced while the other keeps signing.

## How a sync goes

Each call trades the net state (cards and Rev's notices), then each side sends what it has queued for the other. Every post the receiver takes is answered `ACK`; one it refuses is answered `NAK` with one word:

| Word | Meaning | Sent again? |
| --- | --- | --- |
| `bad` | Not a valid record, a bad path, or not a Sysop Net area | No |
| `date` | Dated outside the keep window (only with the clock set) | No |
| `banned` | Its board is banned | No |
| `rejected` | Its board is blocked on the receiving board | No |
| `sig` | The signature does not match the card the receiver holds | No |
| `revonly` | Posted in a Rev-only area by a board that is not Rev's | No |
| `killed` | Killed by Rev | No |
| `limited` | Over its board's daily limit | No |
| `nocard` | The receiver does not hold the posting board's card yet | Yes, next call (unless the sender has no card for that board either) |
| `store` | The receiver's message base did not take it | Yes, next call |

After a `nocard` or `store` the sender stops sending for the rest of that call and tries the same post again next call. If the same post is refused that way three calls in a row, it is passed over for that peer. Before the first post from a given board in a call, the sender sends that board's card, so `nocard` is rare. If the sender holds no card for the posting board (it was pushed out of a full card list), it had nothing to send ahead and no later call can fix that: a `nocard` for that post is final, the post is passed over at once and the call goes on.

A new board asks for history: up to 200 posts per area from the last 30 days, sent oldest first across all four areas together. It keeps asking on every call until one pull has run to its end with at least one post in it, so a pull cut short (a dropped call, a time limit, a memory pause) is asked for again next call; posts it already has are skipped. A board updated from a build without this flag that already holds Sysop Net posts counts its history as done. A call in which either side asks for history gets the full 10 minutes, even from a board that is not trusted yet (see below); the 10-minute wait for that address afterwards still applies.

Each board counts at most 48 posting boards a day against the daily limits; when more show up, the one heard from longest ago is dropped from the count. Posts from Rev's boards are never limited.

Without a clock, posts are trimmed by count only, once every 24 hours the board has been up. Notices and seen marks made before the clock was set are dated to the day the clock is first set.

### Busy and back-off

- One call in and one call out at a time. While the board is short of memory it stops reading the call (the other board waits) and its silence timer stands still; a call still ends after 10 minutes.
- A call from a board this board does not trust yet gets 2 minutes instead of 10, unless either side asks for history. When such a call ends, that address is answered `BYE busy` for 10 minutes. Trusted means: a board this board dials itself, or a board that dialed in and has had 2 calls that ran to the end (both sides said `DONE`). Dialing in once does not make a board trusted, so a stranger's second call is still a stranger's call. A board that only ever dials in gets the short time and the 10-minute wait for its first two completed calls, then the full 10 minutes. Whether a board was trusted is decided as its call starts. Link peers saved by an older build count as trusted.
- An address whose call fails before it proves which board it is gets `BYE busy` for 60 seconds, doubling on each repeat up to 30 minutes.

## A board without an SD card

The net record lives in one file, `/net/record.log`, capped at 64 KB. When it fills, the newest half is kept and the older half is dropped. A board with a card keeps a file per month with no such cap.

## Security model

- Everything is signed, nothing is encrypted. Anyone on the path can read the traffic.
- The dialer proves who it is first, then the answering board proves itself.
- A relay (a middleman passing calls through) is possible only for someone on the path between two boards, or once on the very first contact when `net_first_peer` has no `@board`. Set the `@board` form to close that.
- A key leaked before it is rotated is a full compromise of what that key can do. Treat it as a full compromise until a firmware update carries new built-in public keys.
