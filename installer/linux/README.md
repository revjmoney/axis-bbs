# AXIS Native on Linux: service install

This runs the `axis` program (the board's BBS compiled for Linux) as a system service
on a Linux box, with the LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM on a USB
serial port.

## Install

Build `axis` first (see the project's build notes), then from this folder:

    sudo ./install.sh --modem /dev/ttyUSB0
    sudo ./install.sh --modem /dev/ttyUSB0 --seed /path/to/card-folder   # first install, with files

Options: `--binary PATH` (default `../../build/axis`), `--modem PATH`, `--seed DIR`.
The script is safe to run again. It prints each thing it does, makes the `axis` user
(no login), adds it to `dialout`, makes `/var/lib/axis` (0700), installs
`/usr/local/bin/axis` and the unit, and enables it. It does not start it.

`--seed` is copied into `/var/lib/axis/sd` only when that folder is empty.

## Before the first start: stop the old bridge

Only one program can hold the modem. If the old GHOSTBOARD `v3bridge.py` is running,
stop it first, from a script file (never `pkill -f` typed inline):

    #!/bin/sh
    for p in $(pgrep -f v3bridge.py); do kill "$p"; done

Save that as `stop-bridge.sh`, run it, and check with `pgrep -f v3bridge.py` that it is gone.

## Start, stop, watch

    sudo systemctl start axis
    sudo systemctl stop axis
    sudo systemctl restart axis
    systemctl status axis
    journalctl -u axis -f

A restart from inside the BBS (the board's restart) makes the program exit with code 3,
and systemd starts it again after 2 seconds. A clean stop stays stopped.

## Ports

- LAN port 2323 is always on.
- The public port is off by default (`public_port=0` in `/var/lib/axis/axis.conf`).
  To open one, set `public_port=NNNN` there and restart.
- Use a direct port forward only. A local tunnel or proxy would make every internet caller
  look like 127.0.0.1 and share one limit.

- In a cluster (follower or mirror main) axis also answers on port 23, LAN addresses only: the
  boards' telnet port, where a full board passes a caller to a free node. The unit allows binding
  ports under 1024 (`CAP_NET_BIND_SERVICE`) for this and nothing else.

Other `axis.conf` keys: `lan_port`, `public_port`, `max_callers`, `max_per_ip`,
`max_per_ip_lan`, `rate_per_ip`, `modem`, `drain_minutes`, `no_main_seconds`, `noon`, `mirror_rate`,
`takeover_wait_seconds`, `mirror_max_age_minutes`, `update_check`, `update_repo`. Edit as root, then restart.

Once a day, axis looks up the newest `pc-v*` GitHub release (not the ESP32 `v*` tags). When one is newer
than this build, `*NEW*` blinks where the waiting screen shows the version. `V` on that screen, a tap on
that spot, or System then Update, shows the notes and the release URL. System, Update check, turns the
check off and on again; that writes `update_check` in `axis.conf` (`0` off, `1` the default) so a restart
keeps it. The last result is kept in `<data>/update-check`, so a restart the same day does not ask again.
A missing network, a timeout or a bad reply is one log line and does not stop the BBS.

## In a cluster of boards

With the same cluster key as the boards (`@CLUSTERKEY`, kept in `<data>/cluster.key`), axis listens
5 seconds at each start for the cluster's main on UDP 2325 (LAN only).

- Heard a main on the new firmware (protocol 2): it starts as a follower. The cluster uses UDP 2325
  and TCP 2324, both from LAN addresses only, and the public port stays off while clustered.
- Heard nothing: it starts as the solo BBS, as before. It sends nothing on 2325 and does not open
  2324, but it keeps listening. When a main on the new firmware appears, the public port closes at
  once, new callers are told to call back in a few minutes, the radio line stops answering, and the
  callers on now get a broadcast every minute. When the last one is off, or after `drain_minutes`
  (default 10), axis restarts and joins as a follower. If that main goes quiet for 60 seconds
  first, the drain is called off and axis stays solo (the public port stays closed until the next start).
- A main on older firmware is not joined: the log says to flash it.
- A follower with no main for `no_main_seconds` (default 60) restarts, and finds itself solo. For
  the next 10 minutes it stays solo and ignores mains (the back-off after a failed join), so the
  public port is open again in that time.
- A follower keeps a full copy of the main's card in `<data>/mirror` (the card mirror): a first
  full copy, then each change once it has been quiet 15 seconds, and a check of the whole card every
  day at `noon` (`HH:MM`, default `12:00`). It copies at `mirror_rate` bytes a second (default 65536),
  a quarter of that while someone is on. You can pre-load `<data>/mirror` from a copy of the card:
  files at the main's size are kept. The view cache (`files/viewpacks`, `files/viewcache`) is not
  copied: the BBS makes it again wherever it runs.
- A check that would remove more than 200 files, or more than a quarter of the mirror, removes and
  copies nothing (a blank or swapped card is more likely than your own deletions) and says
  `check would remove N of M files, not removing: use @CARDMIRROR CHECK FORCE`. It asks again every
  10 minutes. `@CARDMIRROR CHECK FORCE` lets the next check make those removals, once.
- If a main without a card hands main to the mirror, axis waits until nobody is on here (at most
  `takeover_wait_seconds`, default 120) and restarts as the main. It declines when the first copy
  is not finished, when this run has not been in step with the card board, or when it was last in
  step more than `mirror_max_age_minutes` ago (default 60): a stale mirror is never served. The
  takeover is called off if, meanwhile, the card board comes back (or the main changes or stops
  asking).
- `@CARDMIRROR` shows the card mirror: the profile, the standing, whether the first copy is
  complete, the files and bytes it holds, the last check, the changed paths waiting, when it was
  last in step with the card board, and the last error. `@CARDMIRROR CHECK` (as a follower) checks
  the whole card at the next chance.
- While axis is main from its mirror, a card board on older firmware that is also main cannot see
  it (it reads the mirror as no card). axis logs it in capitals, stops being main and restarts as a
  follower (then, hearing an older main, it stays solo). Flash the board.
- While axis is main from its mirror, every card path the BBS writes is noted in
  `<data>/mirror.changes`. When the card board is back and linked, `@HANDBACK` shows what would be
  copied to it, its node, and the callers online. `@HANDBACK GO` (or `@HANDBACK GO <node>` with more
  than one card board) first checks every one of those files on the card against how it was at the
  takeover. If any changed on the card since, it stops before writing anything, lists them, and axis
  stays main. Otherwise it copies the changes (at `mirror_rate`), in up to 3 passes, since the BBS
  keeps running. Anything changed during the last pass goes into `<data>/handback-left/` instead and
  is listed. Then the card board restarts as the main and axis restarts as its follower.
  Its end restarts axis, which drops the callers on it, so `@HANDBACK GO` is refused while anyone
  is on; `@HANDBACK GO NOW` (or `@HANDBACK GO <node> NOW`) starts it anyway.
  `@HANDBACK STOP` stops after the file in hand; axis stays main and `@HANDBACK GO` can run again.
  A folder with nothing in it is not made on the card.
- After a hand-back axis is in state `handedback`: it does not take main and has no watchdog until
  it follows the card board as main. If that has not happened in 30 minutes, it goes back to state
  `follower` and the usual rules apply (the watchdog, a takeover when asked).
- A file named `<name>.ax_` on the card is a hand-back's recovery copy: the card would not take the
  new file under its own name after the old one was removed, so the new one is kept there. Rename it
  to `<name>` by hand.
- If `@HANDBACK` keeps stopping on a file (it lists the card's size and mtime now and at the
  takeover) and you want this computer's copy to win over the card's, edit that file's line in
  `<data>/mirror.takeover` while no hand-back runs, then `@HANDBACK GO` again. Its fields are
  split by the 0x1F byte. Put in the card's numbers:
  `sed -i 's|^msgs/x.dat\x1f.*|msgs/x.dat\x1f<size>\x1f<mtime>|' mirror.takeover`. If the card
  shows it missing or a folder, delete the line instead:
  `sed -i '\|^msgs/x.dat\x1f|d' mirror.takeover`. The card's version of that file is then lost.

## Run it in a terminal or a window instead

As your own user (the program refuses to run as root), with its own data folder:

    axis --terminal --data ~/.axis
    axis --window --data ~/.axis          # add --scale 1-6 to size the window
    axis --headless --data ~/.axis        # no screen, like the service

Add `--modem /dev/ttyUSB0` to use the modem, and `--seed DIR` on the first run.
Stop the service first if it holds the same modem or ports: `sudo systemctl stop axis`.

## Hardening the i5

`harden-i5.sh` locks down the box itself (the unit below only locks down the program).
Look at the plan first; a dry run needs no sudo and changes nothing:

    ./harden-i5.sh --dry-run
    sudo ./harden-i5.sh

What it does, in this order:

1. Works out the LAN subnet from the default route's interface, the Remote Desktop server in
   use (xrdp or GNOME Remote Desktop) and its port, any VPN interface (tailscale, wg, zt, tun),
   and `lan_port` and `public_port` from `/var/lib/axis/axis.conf`. It prints all of it, and what
   else is listening that gets no rule.
2. Asks you to confirm the LAN subnet (y/N). `--yes` skips only the second "apply?" question,
   never this one.
3. Installs fail2ban (and ufw or unattended-upgrades if missing) with apt.
4. SSH: only if your `~/.ssh/authorized_keys` has a key, writes
   `/etc/ssh/sshd_config.d/10-axis-hardening.conf` (no passwords, no keyboard-interactive,
   no root login), checks it with `sshd -t`, and reloads ssh. Open sessions stay up. If the
   check fails, the file is removed and the script stops.
5. Firewall (ufw), only adding rules, each tagged `axis-harden`: SSH and Remote Desktop from
   the LAN (and on a VPN interface if there is one), the LAN BBS port, panel 2326, cluster
   UDP 2325 and the cluster link TCP 2324 from the LAN, and the public BBS port from anywhere only
   if `public_port` is set.
   Then default deny incoming, allow outgoing, and enable. The allow rules always go in first.
   Before the first change it saves `ufw status verbose` to `/root/axis-harden-ufw-before.txt`.
6. fail2ban `sshd` jail: 1 hour ban after 5 failures, never banning the LAN or VPN ranges.
7. Automatic security updates (unattended-upgrades).
8. Prints `ss -tulpn`.

After it runs, open a new SSH session and a Remote Desktop connection before closing the old one.

Something else listening (the plan lists it, with ready-to-paste commands) can be kept open
to the LAN with `--allow-lan-port PORT` or `--allow-lan-port PORT/udp` (repeat for more).
Without sudo the dry run cannot read `axis.conf` or the current ufw rules; `sudo ./harden-i5.sh --dry-run`
shows the exact plan and still changes nothing.

fail2ban never bans the LAN (or VPN) ranges, since after this script SSH is reachable only from
there and a ban would be you.

To undo:

1. Firewall rules: every rule the script added is tagged `axis-harden`.

        sudo ufw status numbered
        sudo ufw delete N

   The numbers shift after each delete, so list again before the next one.
2. Firewall default policy: the first real run saved what ufw looked like before to
   `/root/axis-harden-ufw-before.txt`. Put it back from there: if it said
   `Status: inactive` (or ufw was not installed), `sudo ufw disable`; if its `Default:` line said
   `allow (incoming)`, `sudo ufw default allow incoming`; likewise for outgoing.
3. SSH:

        sudo rm /etc/ssh/sshd_config.d/10-axis-hardening.conf && sudo systemctl reload ssh

4. fail2ban: `sudo systemctl disable --now fail2ban` and `sudo rm /etc/fail2ban/jail.d/axis-sshd.local`.
   To unban one address instead: `sudo fail2ban-client set sshd unbanip ADDRESS`.

To go public, forward the public port on the router straight to this box (a direct port
forward). That is the supported way; a tunnel or proxy makes every caller look like one address.

## What the unit locks down

Runs as user `axis`, no new privileges, no capabilities, read-only system, no home folders,
private /tmp, writes only to `/var/lib/axis`, and only IP and local sockets. The modem is opened
through normal file permissions (the `dialout` group), so the program can pick it up again after
a hot-plug. Limits: 4096 open files, 512 MB memory.
