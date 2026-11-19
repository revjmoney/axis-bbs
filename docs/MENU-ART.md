# Menu art and lightbars

The menus are ViSiON/3-style files in `/menus` (`MAIN.MNU`, `MSGMENU.MNU` ...). A `.MNU` holds the commands, the
prompt and the lightbar items (`bar=X,Y,HI,REG,KEY,TEXT`). Its screen is drawn first: from the theme folder when the
theme has one (`MAIN.ANS`, `MATRIX.ANS` for PDMATRIX, `FILES.ANS` for FILEM, `DOORS.ANS` for DOORSM, `MSGS.ANS` for
MSGMENU), else the menu set's own `NAME.ANS`. Then the bars are painted over it, then the prompt.

One set of `.MNU` files serves every look, so every look leaves the same spot for them.

## The menu window

| | |
|---|---|
| Rows 9 to 21, columns 4 to 44 | blank in every menu screen of every look. The lightbars and the prompt go here. |
| Row 22, column 4 | the "arrows move, Enter picks, or press the key" hint, drawn by the art |
| Row 6 | `%BN  node %NN` (BBS name and node), drawn by the art |

The starter set (`tools/menus/make_starter.py`) lays out:

- bars from row 10: two columns of 19 cells at columns 5 and 25 on MAIN, one column of 30 at column 5 elsewhere
- the front door (PDMATRIX, FASTLOGN) at column 12 from row 12, with no prompt
- the prompt at column 5, row 19, by starting `prompt1=` with `|XY05,19`; a prompt with no `|XY` prints under the
  last bar at column 1, over the art's left frame
- a menu note ("Coming soon to AXIS.") on the line under the prompt, from column 5

## Rules for a new look or menu

- Keep rows 9 to 21, columns 4 to 44 blank on MAIN, MATRIX, MSGS, FILES, DOORS and FASTLOGN.
- Fit 80x25 without scrolling; theme screens under 12 KB, `/menus` files under 8 KB.
- Codes the board fills: `|00`..`|15`, `|B0`..`|B7`, `|XYcc,rr`, `|CL`, the values `|UH |NAME |LEVEL |GL |CALLS
  |TL |NODE |MN |TIME |DATE |BN`, and `%BN %NN %SN %TM %DT %UN %VN`. Anything else prints blank.
- `LOGIN.ANS` needs `|{P}` (handle) and `|{O}` (password) with 16 blank cells after each for the typing.
- Every bar's key needs a `cmd=` line, or picking it does nothing. Give bars only to commands that run on AXIS.

## List templates (.LST)

Three list screens take their layout from a list template, a small text file in the look's folder or in `/menus`
(the same places as the menu screens: the theme folder first, then its older folder name, then `/menus`):

| File | Screen |
|---|---|
| `FILES.LST` | the file list of an open file area (not the area list, and not Net Files) |
| `MSGS.LST` | the message list of an open message area (not the area list) |
| `USERS.LST` | the user list (Users) |

`art/menus` ships `FILES.LST` and `MSGS.LST`, which reproduce the built-in lists, so the default look is unchanged and
a look restyles a list by dropping its own file in its folder. No `USERS.LST` ships: the built-in user list is the
default, and a look that wants its own adds one (an example is below). A missing file, or one that does not parse (no `row=`, or a `geom=`
that is not four whole numbers), gives the built-in list; a bad file is logged once. A lite caller (see below) always
gets the built-in list, so a template never makes a radio caller's list heavier.

One `key=value` per line; a line starting with `;` is a comment, and keys the board does not know are skipped.

| Key | Meaning |
|---|---|
| `geom=x,y,width,rows` | where the list sits: its first row's column and row, its width, how many rows. Kept on the screen: x 1 to 79, the width to the right edge, y 4 to 21, the rows to row 21. |
| `colours=hi,reg` | the lightbar's colours as DOS attributes (bg*16+fg): the highlighted row and the others (default `111,15`). Not used by the user list, which has no lightbar. |
| `head=KEY` | the head art's name (the top 3 rows of `KEY.ANS`); default: the area's name, then the banner (the user list: `USERS.ANS`, then the banner). |
| `caption=` | the column caption, on row y-2 (row 4 at the highest). It has no colour of its own: start it with one (`\|08`). |
| `rule=1` or `rule=0` | the rule in the look's accent colour on row y-1, `width` wide (default 1). It needs y-1 below the caption row, so y 6 or more. |
| `row=` | one list row (required). |
| `status=` | the status line, on row y+rows+1 (empty: the built-in line for the highlighted row). Not used by `USERS.LST`. |
| `keys=` | the key line, two rows under the status (empty: the built-in keys, which run to the screen edge). Not used by `USERS.LST`. |

The caption, row, status and keys are art: `|00` to `|15` colours, `|B0`..`|B7`, `{{ACS}}...{{/}}`, plus the screen's
fields. Other values (`|UH`, `%BN` ...) print blank in a list line.
A field is `|XX` (as it is), `@XXnn@` (cut or padded with spaces to exactly nn columns, left aligned) or `@XX>nn@`
(the same, right aligned). Every field value is shown exactly as typed (`50% off`, `meet @ 5`, `|15` all show as
they are): a value is never read as a code, and only control characters in it (ESC, tab, CR, LF ...) show as `?`, so
a value can never move the cursor. A line is cut at `width` columns. In
`FILES.LST` and `MSGS.LST` the rows sit on a lightbar that paints them in `colours=`, so keep colour codes out of
`row=` there (a colour code would cut through the highlight).

The page follows the template: a page (or message window) is `rows` plus one, as the built-in list scrolls one row in
its 14 (15 to a page).

| Screen | Row and status fields | Caption fields |
|---|---|---|
| `FILES.LST` | `FN` name, `SZ` size (`48K`), `AB` the description (40 wide) | `PG` the page (`page 1/3`) |
| `MSGS.LST` | `NW` `*` when new to this caller, `NM` number, `FR` from, `TO` to, `SU` subject | `CT` the count (`12 msgs`) |
| `USERS.LST` | `HN` handle, `LO` location, `CA` calls, `TG` tagline (22 at most), `TW` the twit marker (`  twit`, only ever on the sysop's own view) | none |

The user list stays a listing (no lightbar): its rows go from (x, y), at most `rows` of them, and "and N more" goes on
the status row. The default `FILES.LST`:

```
geom=3,6,74,14
colours=111,15
caption=|08Name          Size   About                         |PG
rule=1
row=@FN12@ @SZ>7@  |AB
```

An example `USERS.LST` for a look's folder (the built-in user list stays the default when a look has none). Its caption
lands on row 4 and its rows from row 6, a row lower than the built-in list, and its colours are fixed, so pick them for
the look (`|06` is the Axis orange). The tagline is sized, so a sysop's twit marker after it never moves it:

```
geom=4,6,76,16
caption=|08handle               from                calls  tagline
rule=1
row=|15@HN20@ |07@LO19@ |06@CA>5@  |08@TG22@|TW
```

## Lite art for slow radio callers

A radio caller on the LORA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM tells the board its air speed (telnet option
90, `modem_link=1`). At or below `lite_below_bps` (7031 by default, so SF9 and slower; 0 turns it off) the caller gets
lite screens: the same menus, bars, keys and features, just lighter art (and the built-in file, message and user
lists, never a look's `.LST`). The RotatoCAM login splash is skipped and ANSI movies in the pack viewer are not paced.
A retrain to a faster tier gets the normal art from the next screen on, and a retrain down gets lite art.

To make lite art, save it beside the full screen with `.LITE` before `.ANS`: `MAIN.LITE.ANS` beside `MAIN.ANS`,
`MATRIX.LITE.ANS`, `STATS.LITE.ANS`, `SAYLOR.LITE.ANS` and so on, in the theme folder or in `/menus`, the same
places the full screens live. A screen with no lite file shows the full one, so a look needs none to work. Keep a
lite screen to the same window as the full one (the bars and prompt land in the same spots), and keep it small:
plain colours and few cursor moves, about 1 to 2 KB, since SF12 carries roughly 140 bytes a second.

## Checking

```
python tools/art/check_menus.py            # every look in local/sdstage/themes, and art/menus
python tools/art/check_menus.py --show     # also print what each bar lands on
```

It renders each screen, checks the size, scrolling and codes, checks each bar lands on blank cells or on the art's
own words for that item, and checks each `.MNU` (bars in the window, a command behind every key, the prompt placed
in the window with known codes). CI runs it on `art/menus`.
