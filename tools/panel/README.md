# AXIS sysop control panel

A small app for your PC that finds every AXIS board on your home network and serves a sysop
control panel to any browser on that network (your PC or your phone).

```
pip install aiohttp pystray pillow
python tools/panel/run.py
```

It prints the address to open, for example `http://192.168.1.50:8326/`, and puts an AXIS icon in
the tray (Open panel, Quit). Log in with your sysop handle and password, or any user at security 255.

- Boards are found by the announcements they already send each other (UDP 2325); each board's panel
  port is TCP 2326, LAN only. `--board 192.168.1.164` adds a board by hand.
- Tabs: **Nodes** (who is on every node, note, +15 minutes, send to the teleconference, kick, a
  broadcast), **Users** (validate, deny, security, password, permissions, twit), **Settings** (every
  setting, the sysop password and the new user password), **Lists** (rumors, oneliners, BBS list,
  votes), **System** (each board's status, web and No-IP, cards, event log, and the live event feed).
- **Wall**: every node's screen, live (the boards only stream while the Wall or Console tab is open). Tap a tile to open it in the Console.
- **Console**: open a new sysop session on any board (it takes a free node, headless boards too), or break in on a caller: you see their screen and type into it, and they're told when you break in and when you leave.
- Every button runs the same `@` command you can type on a board's serial console.
- Your password is used only to answer each board's login challenge; the app never saves it.

Tests: `cd tools/panel && python -m pytest -q`
