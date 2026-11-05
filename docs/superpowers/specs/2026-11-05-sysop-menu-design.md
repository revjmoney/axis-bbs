# In-BBS Sysop Menu: Design

Date: 2026-11-05. Requested by Rev (sysop, AXIS UNDERGROUND CNC): "all local console menus and settings
should feel like part of the bbs, all bbs settings should be in the sysop menu in layers", then "everything
in the online sysop menu inside the board, full bbs control: users, edit file areas and manage files, edit
and manage message bases, all settings, edit menus, etc".

## Intent

One sysop menu that is part of the BBS itself: an ANSI screen in the board's look (frames, lightbars,
theme colours), reached the same way from a telnet call and from the board's own screen, with layered
menus that reach every setting and every piece of BBS data. Today the touchscreen sysop menu is a
separate-looking app with a few settings, the telnet `*` editor covers only users, and most control lives
in serial `@` commands.

## Goals

1. **One menu, in the BBS.** The sysop menu runs inside a BBS node as ANSI screens in the current theme
   (axis, oblivion or iniquity): Iniquity/Oblivion-style frames, lightbar items, the theme's colours.
   - Online: `*` at the main menu (sysop only), replacing today's user editor.
   - On the board: the WFC's **f2 sysop menu** opens the local console straight into it (the local
     console already shows the BBS screen). Touch drives it: tap an item, or the on-screen keys.
   - The current touch sysop screens are retired once everything they do is in the new menu (the WFC's
     f3..f7 open the matching layer).
2. **Layers** (lightbar menus, Esc or Q goes back up a layer):
   - **Users:** list and search; edit every field (handle, real name, location, email, tagline, security,
     permissions, free downloads, twit, time and ratio); validate or deny; set a password; delete; add.
   - **File areas:** add, edit (id, name, level, mirror), reorder, remove. Inside an area: list files;
     edit descriptions; move to another area, rename, delete.
   - **Message bases:** add, edit (id, name, level), reorder, remove. Inside: read; delete, move, edit
     posts; purge posts older than N days.
   - **Settings:** every `axis.ini` setting in groups: BBS and sysop, network (telnet port, public host,
     No-IP, web extras), callers and limits (new user security, daily minutes, idle, ratio, NUP), doors,
     theme, cluster and cards. Passwords can be set but are never shown.
   - **Menus:** for each menu (main menu first; others as they become data), list its items; add, remove,
     reorder; edit an item's hotkey, name and command (picked from the list of BBS commands).
   - **System:** event log, nodes (kick, add time, note), cards, restart, SD check and format, LORD Setup,
     web and No-IP status, the panel's status.
3. **Same result everywhere:** a change made here, on the serial console or on the PC control panel does
   the same thing (each action reuses the existing code path: `runCommand` and the stores).
4. **Changes reach every board** the way `@SET` does today (saved on the main, shared).

## Non-goals and anti-goals

- No second copy of the logic for the touch path: touch drives the same BBS screens.
- Never show a password or hash.
- A sysop on a phone telnet client must still be able to use it (keys only, 80x24).
- Must not crowd out the caller-side nodes or make the main run out of memory (about 42 KB free).

## Decided by Rev

- The menu editor edits a menu's items: renaming them, choosing which commands appear, and their hotkeys.

## My calls (Claude's, Rev can overrule)

- **Menus become data.** Each editable menu is a list of items `hotkey|name|command`, stored in
  `axis.ini` as `menu.main.N=...` lines (shared like other settings). The built-in menus are the
  defaults. Commands come from a fixed list of what the BBS can do (messages, files, doors, chat, who's
  online, email, scan, oneliners, rumors, BBS list, top ten, voting, account, multinode, page sysop, info,
  credits, goodbye, sysop menu). The theme art keys (O/2 letters with a MAIN.ANS) keep working: when a
  theme's MAIN.ANS is present, its fixed letters apply, and the editor says so.
- **Deleting is guarded:** a second confirmation for deleting users, areas, files and posts.
- **Order of work:** the menu shell and Users first, then Settings, then File areas, Message bases,
  System, and the Menu editor last.

## Left to the builder

- Screen layouts inside each layer, field editing widgets (line edit, toggles, number pickers), paging.
- How the WFC's f-keys map onto the new layers.
