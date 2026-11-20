# axispc on Linux: package, AppImage and service

`axispc` is the AXIS BBS built as a native PC program (the same shared core the ESP32 board
runs), on one event loop thread with no 4 session cap. CI builds it for x86_64, ARM64 and
32-bit ARM (armhf) as a Debian package (`.deb`) and an AppImage.

It uses plan 1's data folder layout, so a folder the Linux `axis` program made works as it is:

* `<data>/sd` : the SD card tree (the BBS card: menus, messages, files)
* `<data>/flash` : the board's LittleFS area (its config `axis.ini` and the user base `users.dat`)
* `<data>/prefs` : the board's Preferences store (the stand in for its NVS, as `<name>.ini` files)
* `<data>/log` : the program's log (`axispc.log`)
* `<data>/axis.conf` : the Linux gate's settings (ports, limits, the modem: `lan_port`,
  `public_port`, `modem`, ...)

The default data folder is `/var/lib/axis` for the service user `axis`, else `~/.axis`.

## The .deb package

Install the package built for your architecture (`<version>` is the release, e.g. 0.54):

    sudo apt install ./axispc_<version>_amd64.deb

It installs `/usr/bin/axispc`, a desktop entry, an icon, this README under
`/usr/share/doc/axispc`, and a systemd unit at `/usr/lib/systemd/system/axispc.service`. The
package depends only on `libc6` and `libstdc++6`.

The package ships the service inert: it does not create the `axis` user, does not make
`/var/lib/axis`, and does not enable or start anything. Set that up yourself (see "Run as a
service" below). The package never touches a running service or its data.

## Run it in a terminal

As your own user (the program refuses to run as root), with its own data folder:

    axispc --terminal --data ~/.axis          # the sysop WFC screen here
    axispc --headless --data ~/.axis          # no screen, the log on stdout

Add `--modem /dev/ttyUSB0` to answer the LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM
(LFD3K-SM) on a serial port, and `--seed /path/to/card-folder` on the first run to fill an
empty `<data>/sd`. SIGTERM, SIGINT or SIGHUP stops it: every caller is hung up, the users and
stats are saved, exit 0.

## The AppImage

The AppImage is one file, no install needed (it bundles axispc, not the C library, so it needs a
host about as new as Ubuntu 24.04). Make it executable and run it:

    chmod +x axispc-<version>-x86_64.AppImage
    ./axispc-<version>-x86_64.AppImage --terminal --data ~/.axis

On a host without FUSE, run it with `--appimage-extract-and-run` or set
`APPIMAGE_EXTRACT_AND_RUN=1`.

## Run as a service

The unit runs `axispc --headless --data /var/lib/axis` as the user `axis`. It shares that user
and that data folder with the native `axis` service, so only one of them can run at a time: if
the `axis` service is installed, stop and disable it first (`sudo systemctl disable --now
axis`), or they will fight over the ports and the modem.

1. Make the service user and its data folder (once):

        sudo useradd --system --no-create-home --home-dir /var/lib/axis --shell /usr/sbin/nologin axis
        sudo usermod -aG dialout axis
        sudo install -d -m 0700 -o axis -g axis /var/lib/axis

   Add the modem with a `modem=/dev/ttyUSB0` line in `/var/lib/axis/axis.conf` (owned by
   `axis`, mode 0600).

2. Enable and start it:

        sudo systemctl enable --now axispc
        systemctl status axispc
        journalctl -u axispc -f

### Ports

* `lan_port` (default 2323) is always on. It needs no special privilege.
* `public_port` is off by default (0). To open one, set `public_port=NNNN` in
  `/var/lib/axis/axis.conf` and restart. The unit allows binding a port under 1024
  (`CAP_NET_BIND_SERVICE`) for that and nothing else.
* Use a direct port forward for a public port. A tunnel or proxy makes every internet caller
  look like 127.0.0.1 and share one limit.
