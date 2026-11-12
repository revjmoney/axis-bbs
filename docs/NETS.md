<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Rev. J. Money -->
# Sysop Net

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

The `@board` part of `net_first_peer` is the first peer's board id, 16 hex characters (shown by `@NET` on that board). With it set, a call to that address must prove it is that board or the call is dropped. Without it, this board trusts whichever board answers the first time and remembers that board id for later calls. The value is never shown in clear on screen; the sysop pages say `set (hidden)`.

## Commands

Type them over serial, in the sysop menu, or in the PC panel.

Status:

- `@NET` shows the board id, the net state and counts. Example: `@NET`
- `@NET SYNC` dials the first peer soon instead of waiting. Example: `@NET SYNC`
- `@NET PEERS` lists known peers and when each was last reached. Example: `@NET PEERS`
- `@NET RECORD [n [skip]]` shows the newest n lines of the net record (default 20, most 100), newest first. The optional second number skips that many of the newest lines first, so you can page back through older ones. Examples: `@NET RECORD 50`, and `@NET RECORD 20 20` for the 20 lines before the newest 20
- `@NET LOCAL` lists what this board has hidden or blocked.

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

## Files

Under `/net/` on the storage root:

- `record/<yyyymm>.log` is the net record, one file per month (SD card). Without a card it is the single `record.log`.
- `outlog.txt` is the log of what this board still has to pass on.
- `local.txt` holds this board's own hide and block lines.
- `firstpeer.txt` remembers the first peer's address and the board id learned from it.

In internal flash:

- `/netkey.txt` is this board's own signing seed, made on first boot.
- `/netrev.key` is Rev's seed, written by `@NETKEY SET`. It wins over `/netkey.txt` when present.

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

## A board without an SD card

The net record lives in one file, `/net/record.log`, capped at 64 KB. When it fills, the newest half is kept and the older half is dropped. A board with a card keeps a file per month with no such cap.

## Security model

- Everything is signed, nothing is encrypted. Anyone on the path can read the traffic.
- The dialer proves who it is first, then the answering board proves itself.
- A relay (a middleman passing calls through) is possible only for someone on the path between two boards, or once on the very first contact when `net_first_peer` has no `@board`. Set the `@board` form to close that.
- A key leaked before it is rotated is a full compromise of what that key can do. Treat it as a full compromise until a firmware update carries new built-in public keys.
