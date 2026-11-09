# AXIS ANSI packs

Rev. J. Money's AXIS screen sets, as delivered: the 13 screens of each look (splash, banner, welcome, main menu, login,
new user, doors, messages, files, matrix, fast login, shop, mill), previews, and the scripts that built them. The
Underground pack and the two Saylor poster zips carry the tall Saylor clock-in poster.

The installed versions of these screens (renamed for the firmware: MAIN.ANS, MSGS.ANS, FILES.ANS ...) live on a
board's card in `/themes/<look>/`; `tools/art/axis_packs.py` and `tools/art/make_wfc_packs.py` turn them into theme
folders. Board 1 also offers these zips for download in the ANSI file area (8.3 names: AXUNDERG.ZIP, AXCRT.ZIP,
AXHAUNT.ZIP, AXDEEPSE.ZIP, AXCATHED.ZIP, AXSKULLS.ZIP, AXWASTE.ZIP, SAYLORP4.ZIP, SAYLORP3.ZIP).

The CRT Hardcore v2 zip here leaves out `assets/skull-reference.webp`, the third-party reference picture its README
names; nothing in the pack needs it to display or rebuild the ANSI.
