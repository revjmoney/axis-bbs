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

The service has no capabilities, so it cannot bind ports under 1024. Use a high port and forward
to it on the router, or add `AmbientCapabilities=CAP_NET_BIND_SERVICE` in a drop-in
(`systemctl edit axis`).

Other `axis.conf` keys: `lan_port`, `public_port`, `max_callers`, `max_per_ip`,
`max_per_ip_lan`, `rate_per_ip`, `modem`, `drain_minutes`, `no_main_seconds`. Edit as root, then restart.

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
- A follower with no main for `no_main_seconds` (default 60) restarts, and finds itself solo.

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
