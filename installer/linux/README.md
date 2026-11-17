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

Other `axis.conf` keys: `lan_port`, `public_port`, `max_callers`, `max_per_ip`,
`max_per_ip_lan`, `rate_per_ip`, `modem`. Edit as root, then restart.

## Run it in a terminal or a window instead

As your own user (the program refuses to run as root), with its own data folder:

    axis --terminal --data ~/.axis
    axis --window --data ~/.axis          # add --scale 1-6 to size the window
    axis --headless --data ~/.axis        # no screen, like the service

Add `--modem /dev/ttyUSB0` to use the modem, and `--seed DIR` on the first run.
Stop the service first if it holds the same modem or ports: `sudo systemctl stop axis`.

## What the unit locks down

Runs as user `axis`, no new privileges, no capabilities, read-only system, no home folders,
private /tmp, writes only to `/var/lib/axis`, only IP and local sockets, and only USB serial
devices (`DevicePolicy=closed` with `char-ttyUSB`). Limits: 4096 open files, 512 MB memory.
