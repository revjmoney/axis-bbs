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

The `@board` part of `net_first_peer` is the first peer's board id, 16 hex characters (shown by `@NET` on that board). With it set, a call to that address must prove it is that board or the call is dropped. Without it, this board trusts whichever board answers the first time and remembers that board id for later calls. The value is never shown in clear on screen; the sysop pages say `set (hidden)`.

## Commands

Type them over serial, in the sysop menu, or in the PC panel.

Status:

- `@NET` shows the board id, the net state and counts. Example: `@NET`
- `@NET SYNC` dials the first peer soon instead of waiting. Example: `@NET SYNC`
- `@NET PEERS` lists known peers and when each was last reached. Example: `@NET PEERS`
- `@NET RECORD [n [skip]]` shows the newest n lines of the net record (default 20, most 100), newest first. The optional second number skips that many of the newest lines first, so you can page back through older ones. Examples: `@NET RECORD 50`, and `@NET RECORD 20 20` for the 20 lines before the newest 20
- `@NET LOCAL` lists what this board has hidden or blocked.
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

The application carries the board's id, its public key, the card's name, sysop and location, the board's MAC, your blurb (up to 240 characters, line breaks allowed) and a number counting that board's applications. It is signed with the board's own key and travels over Sysop Net. `@NETAPPLY` is refused when `net_user` is off, when the board is already a member or has already applied, when it is vetoed, while the clock is not yet set, and for 30 days after a deny. A reply of `@OK applied` means it was taken here and will be passed on.

### How to vote

Only member boards vote. List who is waiting with `@NET WAITING`, read one with `@NET APP <board id>`, and vote:

```
@NETVOTE <board id> Y|N [comment]
```

The comment is up to 72 characters. A board cannot vote on its own application. A second vote on the same application replaces the first. Each vote is signed with the voting board's own key and checks by itself, so no card is needed to verify it.

### The counts

Every board tallies the signed records itself, so all boards reach the same result with no server. Only votes from boards that are members by that same tally count. By default 3 yes votes admit an applicant and 3 no votes deny it. Rev can change the two counts, each 1 to 20, with `@NETVOTES yes=<n>,no=<n>`. Only the latest application of a board counts, and a new application starts a new tally. At most 16 applications and 256 member votes are held.

### Rev's admit, deny and veto

On Rev's board:

- `@NETADMIT <board id>` admits the applicant at once.
- `@NETDENY <board id>` denies it at once. The board may apply again after 30 days.
- `@NETVETO <board id> YES` keeps it out for good, and beats any vote count. A veto can come after a board is in, and then it leaves User Net. Without `YES` the command only explains what it would do.

Each is a signed notice every board checks against Rev's public keys. Rev's board holds up to 64 such notices; past that a new board is refused, never an old one dropped.

### Auto seal

When the count on a board admits or denies an applicant, Rev's board signs the matching admit or deny notice itself (four a pass at most), so the result is fixed for every board and does not depend on who voted later. Rev's own boards and boards Rev admitted are always members.

### Carrying the areas

A member board with `net_user` on carries the areas in `net_user_areas`. Callers at `net_user_level` or above see them in the message menu as their own User Net group. A caller whose net posts permission is off (`@PERM <handle> nets off`) cannot post to either net, and neither can a twit. A board that is not a member, or does not carry an area, refuses its posts. A board that becomes a member, or starts carrying another area, asks its peers for the User Net pull again (see below).

### Syncing User Net

Boards that have User Net say `net2` in the links field of their card. Only a peer whose card has `net2` is sent User Net records and the User Net pull, so an older board never sees them. A new member, or a board with a new area, asks with `UWANT`; the peer answers with the applications and votes it holds as `U` lines, then the newest posts of the areas asked for, then `UEND`. The pull is asked for on each call until one reaches `UEND`. Applications and votes are passed on to every net2 peer whether or not this board is a member.

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

The signed application and vote lines are kept whole in `/net/apps.txt` on the storage root, one line each, appended as they are taken. The board holds only compact fields in RAM and reads a line back by its byte offset, so the file is never read whole at run time. It is read a few lines a pass at boot. Past twice the size of what is held (and 64 KB), the file is rewritten with only the lines still held, streamed through a temp file; the old file steps aside as `apps.old` until the new one is in place, and a restart after a cut puts the right one back.

## The PC test peer

`axis_netpeer` (built with the PC build) is a net node on the PC, for testing a board against something other than a board. Besides `post`, `peers`, `sync` and `list`, it speaks User Net with these commands, one per line on stdin:

- `user on|off` carries all four User Net areas and says `net2` on the peer's card, or stops. It is remembered in `user.txt`.
- `apply <blurb>` applies, as `@NETAPPLY` does (`\n` in the blurb is a line break). The peer needs `user on` first.
- `vote <board> y|n [comment]` votes, once the peer is a member.
- `apps` lists the applications held, with the counts. `members` lists the members.

The peer keeps the signed lines in `apps.txt` in its folder, and its application and vote counters in `appseq.txt` and `voteseq.txt`. It uses the board's own tally, so it counts exactly as a board does. It is never one of Rev's boards, so it does not seal.

## Files

Under `/net/` on the storage root:

- `record/<yyyymm>.log` is the net record, one file per month (SD card). Without a card it is the single `record.log`.
- `outlog.txt` is the log of what this board still has to pass on.
- `local.txt` holds this board's own hide and block lines.
- `firstpeer.txt` remembers the first peer's address and the board id learned from it.
- `apps.txt` holds the signed User Net applications and votes (see "/net/apps.txt").

Also under `/net/`: `state.txt` (cards and Rev's notices), `seen.txt` (which posts this board has seen) and `cursors.txt` (how far each link peer has been sent, how many calls with it completed, and whether this board's history pull is done).

In internal flash:

- `/netkey.txt` is this board's own signing seed, made on first boot.
- `/netrev.key` is Rev's seed, written by `@NETKEY SET`. It wins over `/netkey.txt` when present.
- `/netseq.txt` holds the next post number this board will use, its card's number, and the next vote and application numbers, beside the key. It lives in internal flash so a board that boots without its SD card keeps counting where it left off; peers drop a post whose number they have already seen. The new number is written before a post is stored or sent, so a reset can skip a number but never use one twice. If the file is missing and the clock is set, both numbers start from the current time (seconds since 1970), which is above any number used before. Older boards kept these as `/net/postseq.txt` and `/net/card.txt`; those are read once as a floor.

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
