# AXIS ANSI packs

Rev. J. Money's AXIS screen sets, as delivered: the 13 screens of each look (splash, banner, welcome, main menu, login,
new user, doors, messages, files, matrix, fast login, shop, mill), previews, and the scripts that built them. The
Underground pack and the two Saylor poster zips carry the tall Saylor clock-in poster.

Each of the seven look zips also carries an `axis/` folder. `tools/install_cyd.py` copies it into the theme folder
under the firmware names: `GOODBYE.ANS` (the logoff screen), `FILES.LST`, `MSGS.LST` and `USERS.LST` (the list
templates), `THEME.CFG` (`dialect=axis`), and a `.LITE.ANS` beside each menu a slow radio caller draws. Those lite
screens stay under 2 KB, with the same menu window as the full art. The house Axis look ships the same files in
`art/menus/`. `tools/art/make_look_extras.py` rebuilds them. The Underground pack's `SAYLOR.LITE.ANS` is the short
screen a slow caller gets instead of the tall poster.

The numbered `ans/` screens are renamed on install (MAIN.ANS, MSGS.ANS, FILES.ANS and the rest) into
`/themes/<look>/` on the card. `tools/art/axis_packs.py` draws the built-in header set, and
`tools/art/make_wfc_packs.py` draws the waiting-for-caller screens. Board 1 also offers these zips for download in
the ANSI file area (8.3 names: AXUNDERG.ZIP, AXCRT.ZIP, AXHAUNT.ZIP, AXDEEPSE.ZIP, AXCATHED.ZIP, AXSKULLS.ZIP,
AXWASTE.ZIP, SAYLORP4.ZIP, SAYLORP3.ZIP).

The CRT Hardcore v2 zip here leaves out `assets/skull-reference.webp`, the third-party reference picture its README
names; nothing in the pack needs it to display or rebuild the ANSI.
