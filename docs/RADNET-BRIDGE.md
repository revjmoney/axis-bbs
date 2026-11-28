<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Rev. J. Money -->
# RadNet bridge (axis_bridge)

`axis_bridge` moves posts between AXIS message areas and [NecroTechs RadNet](https://github.com/longwoodgeek/necrotechs-radnet), an offline-first Python BBS that runs over Reticulum and LoRa. Both sides read and write the same file, a **radnet-bridge v1 bundle** (JSON Lines). RadNet's side is `python -m radnet bridge export|import`; its format page is `docs/axis-bridge-bundle-v1.md` in that repository.

It is a PC tool, built with the PC build like `axis_netpeer`. It is not in the board image. Nothing is sent over a network: carry the file however you like.

## Use

```
axis_bridge export --msgs <dir> --station <board id> [--config <axis.ini>] <out.jsonl> <area>[=<title>]...
axis_bridge import --msgs <dir> [--area <area>] <in.jsonl>
```

- `<dir>` is a message-base folder: a copy of the card's `/msgs`, or AXIS Native's `<data>/sd/msgs`.
- **Export** writes the named areas to a new file and never overwrites one.
  - `--station` is this board's id (the 16 hex characters `@NET` shows), or any short name using letters, digits, `.`, `_` and `-`.
  - Area titles come from `area=Title` on the command line, or from the `msgareaN=id|name` lines of `--config`.
- **Import** posts the bundle's posts into one area, `radnet` unless `--area` says otherwise. Add that area to the board's message areas (`msgareaN=radnet|RadNet`) so callers see it.
  - Run import while the BBS is stopped, or into a card copy you put back. The BBS keeps its own place in each area file.

Example, both ways:

```
axis_bridge export --msgs card/msgs --station 0a1b2c3d4e5f6789 --config card/axis.ini to-radnet.jsonl general cnc sn_support
python -m radnet --db radnet.sqlite3 bridge import to-radnet.jsonl

python -m radnet --db radnet.sqlite3 bridge export from-radnet.jsonl
axis_bridge import --msgs card/msgs from-radnet.jsonl
```

## What it does with posts

**Export** (AXIS to RadNet):
- Every area becomes a RadNet board and every post a RadNet post.
- Each post gets an **origin**:
  - A Sysop Net or User Net post is `axis:<poster board>:<post id>`, in board `axis:net:<area>`. That post exported from any AXIS board is the same post to RadNet, and every board's `sn_support` is one RadNet board.
  - A local post is `axis:<station>:<area>.<num>`, in board `axis:<station>:<area>`.
- Text goes from CP437 to UTF-8. Pipe colours `|00` to `|23` and ANSI codes are removed. Text over RadNet's limits (author 32, title 100, body 4000 characters) is cut and marked `[truncated]`.
- AXIS has no reply threading, so `parent` is always null. A reply keeps its `Re:` subject.

**Import** (RadNet to AXIS):
- Every post goes into the one area. Its subject is `[<RadNet board title>] <title>`, cut to 72 characters.
- From is the RadNet author, To is `All`, the date is the time of the import.
- Text goes from UTF-8 to CP437. A character CP437 lacks becomes `?`; dashes and curly quotes become their plain forms.
- The body ends with `[via radnet, unverified]`.
- Posts whose origin starts `axis:` are skipped: they are AXIS posts already.
- Each imported post is written to `<area>.bridge` beside `<area>.msg` (`<num> <origin>` per line). That file is how a second import skips posts it already took, and how export knows not to send them back to RadNet.

## Limits

- **Nothing is signed or checked.** A bundle can claim any author. That is why imported posts say "unverified". Imported posts are local posts: they never go out on Sysop Net or User Net.
- **No deletions travel** either way. A post removed on one side stays on the other.
- **No live link:** run it by hand, or from a job you control.
- Bundle caps match RadNet's: 8 MB, 16 KB per line, 5000 records. A record that breaks the format is reported by line number and skipped; a bad header refuses the whole file.

## Files

- `tools/bridge/bridgebundle.h`, `.cpp`: the format, text conversion, export and import (unit tested in `tests/test_bridgebundle.cpp`).
- `tools/bridge/main.cpp`: the command line.
