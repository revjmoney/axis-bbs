# AXIS Underground CNC: BBS directory listing (Telnet BBS Guide)

Copy and paste into the listing form. The address fields are left for you on purpose: type the
telnet address and port yourself (the repo and the website never carry them).

## The fields

| Field | Value |
|---|---|
| BBS name | AXIS Underground CNC |
| Sysop | Rev. J. Money |
| Location | Myrtle Beach, South Carolina, USA |
| Telnet address | (yours to type) |
| Port | (yours to type) |
| Connection | Telnet |
| Software | Axis Micro BBS Firmware 0.51 (pick "Other" if it isn't in the list) |
| Nodes | 4 |
| Online since | November 2026 |
| Website | https://revjmoney.github.io/axis-bbs/ |
| Also | https://jmscnc.com/axis-bbs/ |
| Listing image | `card-400-crt-main.png` (the crt main menu) or `card-400-board-photo.jpg` (the board itself) |

## Short description (the part the list card shows, about 330 characters)

A real BBS running on a $15 ESP32 touchscreen board, not a PC. AXIS Underground CNC is a 4 node
cluster of tiny boards acting as one system: full ANSI, eight switchable art packs, Renegade-style
WFC, LORD 4.07, teleconference and split-screen chat, and file areas full of CNC, CAM and G-code.
Pull up a stool, the spindle is warm.

## Full description

AXIS Underground CNC is a bulletin board built the hard way: no PC, no emulator, no Raspberry Pi.
The whole BBS runs on ESP32 "Cheap Yellow Display" boards, the $15 kind with a 2.8 inch touchscreen.
Several of them on one LAN act as a single system, so you can be on node 1 while your buddy is on
node 3 and you'll still share the same message bases, files, chat and oneliners. If the main board
drops, another one takes over.

It's the home board of RotatoCAM (4 axis rotary CNC software) and the JMSCNC shop, so the file areas
lean hard into machining: RotatoCAM releases, CNC freeware and shareware, G-code and toolpaths,
3D models, CAD/CAM software, plus ANSI art packs, scene artpacks, Game Boy
homebrew and Rev J Money's music.

What's on the board:

- Full ANSI with eight complete art packs (Axis, CRT, Haunted, Deep Sea, Occult Cathedral, Skulls,
  Underground, Wasteland), each with its own matrix, login, menus and waiting-for-caller screen
- ViSiON/3-style lightbar menus and login sequence, with a sysop clock-in poster
- Legend of the Red Dragon 4.07 (registered) and BBS Black Jack, running on the board itself
- Message bases: General Chatter, CNC Shop Talk, RotatoCAM, Buy Sell & Trade, BBS Scene & ANSI,
  Ask the Sysop, plus private email
- Teleconference, split-screen chat, node notes and broadcasts
- Oneliners, rumors, voting booth, top ten, last callers, a BBS list
- A Renegade-style WFC screen for the sysop, on the touchscreen and over telnet

The firmware is open source (GPL-3.0) and written from scratch for this hardware:
https://github.com/revjmoney/axis-bbs. Coming next: FidoNet and fsxNet mail, and a LoRa mesh
radio bridge.

New users are welcome. Apply at the matrix, and if you get stuck, page the sysop.

## Screenshots (in this folder, 640x400, 80x25 telnet)

| File | What it shows |
|---|---|
| `01-matrix.png` | The front door matrix (crt look) |
| `02-prelogon.png` | A pre-logon screen |
| `03-login.png` | The login screen |
| `04-main-menu.png` | The main menu lightbar |
| `05-wfc-classic.png` | The classic waiting-for-caller screen, with the version |
| `06-look-*.png` | Other looks: underground, skulls, sea, cathedral (sysop preview screens) |
| `07-lord.png` | LORD 4.07, a forest fight |
| `08-files.png` | A file area listing |
| `09-messages.png` | Reading a message |
| `10-split-chat.png`, `11-teleconference.png` | Chat |
| `12-oneliners.png` | Oneliners |
| `13-eight-looks-wfc.png` | All eight looks' WFC screens side by side |
| `14-the-board.jpg`, `15-cluster-family.jpg` | The hardware: one board, and the cluster |
