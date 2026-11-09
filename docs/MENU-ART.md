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

## Checking

```
python tools/art/check_menus.py            # every look in local/sdstage/themes, and art/menus
python tools/art/check_menus.py --show     # also print what each bar lands on
```

It renders each screen, checks the size, scrolling and codes, checks each bar lands on blank cells or on the art's
own words for that item, and checks each `.MNU` (bars in the window, a command behind every key, the prompt placed
in the window with known codes). CI runs it on `art/menus`.
