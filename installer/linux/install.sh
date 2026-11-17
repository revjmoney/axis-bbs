#!/usr/bin/env bash
# Installs AXIS Native as a systemd service. Run with sudo:
#   sudo ./install.sh [--binary PATH] [--modem /dev/ttyUSB0] [--seed DIR]
# Safe to run again: each step checks first and says what it did.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="$here/../../build/axis"
modem=""
seed=""

usage() {
    echo "usage: sudo $0 [--binary PATH] [--modem /dev/ttyUSB0] [--seed DIR]"
    echo "  --binary PATH  the axis program to install (default: build/axis in this checkout)"
    echo "  --modem PATH   serial device of the LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM (written to axis.conf as modem=)"
    echo "  --seed DIR     a card folder to copy into /var/lib/axis/sd (only if sd is empty)"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --binary) [ $# -ge 2 ] || { usage; exit 2; }; binary="$2"; shift 2 ;;
        --modem)  [ $# -ge 2 ] || { usage; exit 2; }; modem="$2"; shift 2 ;;
        --seed)   [ $# -ge 2 ] || { usage; exit 2; }; seed="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage; exit 2 ;;
    esac
done

[ "$(id -u)" -eq 0 ] || { echo "run this with sudo (it makes a user, a folder and a service)" >&2; exit 1; }
[ -f "$binary" ] || { echo "no axis program at $binary (build it, or give --binary PATH)" >&2; exit 1; }
if [ -n "$seed" ]; then
    [ -d "$seed" ] || { echo "the seed $seed is not a folder" >&2; exit 1; }
fi
case "$modem" in
    "" | /dev/*) ;;
    *) echo "--modem must be a device path like /dev/ttyUSB0" >&2; exit 1 ;;
esac
case "$modem" in
    "" | /dev/ttyUSB* | /dev/serial/*) ;;
    *) echo "warning: $modem is not /dev/ttyUSB* or /dev/serial/*; it will still work through dialout, but the V3 normally appears as ttyUSB" >&2 ;;
esac
case "$modem" in
    *[[:space:]]*) echo "--modem must not contain spaces" >&2; exit 1 ;;
esac

if [ -n "$seed" ]; then
    bad="$(find "$seed" \( -type l -o -type b -o -type c -o -type p -o -type s -o -perm /6000 \) -print)"
    if [ -n "$bad" ]; then
        echo "the seed has links, device nodes or setuid/setgid files, so it was refused:" >&2
        echo "$bad" >&2
        exit 1
    fi
fi

data=/var/lib/axis

# 1. The service user: no login, no home.
if id axis >/dev/null 2>&1; then
    echo "user axis: already there"
else
    useradd --system --no-create-home --home-dir "$data" --shell /usr/sbin/nologin axis
    echo "user axis: created"
fi

# 2. dialout, for the serial modem.
if id -nG axis | tr ' ' '\n' | grep -qx dialout; then
    echo "group dialout: axis is already in it"
else
    usermod -aG dialout axis
    echo "group dialout: added axis"
fi

# 3. The data folder.
install -d -m 0700 -o axis -g axis "$data"
echo "folder $data: ready (0700, owned by axis)"

# 4. The program.
install -m 0755 -o root -g root "$binary" /usr/local/bin/axis
echo "program: installed /usr/local/bin/axis from $binary"
if systemctl is-active --quiet axis.service; then
    echo "restart axis to use the new program: sudo systemctl restart axis"
fi

# 5. The modem, written to axis.conf as modem=PATH (other keys are kept).
if [ -n "$modem" ]; then
    conf="$data/axis.conf"
    if [ -f "$conf" ] && grep -q '^[[:space:]]*modem[[:space:]]*=' "$conf"; then
        tmp="$(mktemp)"
        grep -v '^[[:space:]]*modem[[:space:]]*=' "$conf" > "$tmp" || true
        echo "modem=$modem" >> "$tmp"
        cat "$tmp" > "$conf"
        rm -f "$tmp"
        echo "axis.conf: modem= set to $modem"
    else
        # a hand-edited axis.conf may lack its last newline: the new key must not join the last line
        if [ -s "$conf" ] && [ -n "$(tail -c1 "$conf")" ]; then echo >> "$conf"; fi
        echo "modem=$modem" >> "$conf"
        echo "axis.conf: added modem=$modem"
    fi
    chown axis:axis "$conf"
    chmod 0600 "$conf"
else
    echo "axis.conf: no --modem given, left alone (the radio line stays off unless modem= is already set)"
fi

# 6. The seed: copied only into an empty sd folder, so it never overwrites a live card.
if [ -n "$seed" ]; then
    install -d -m 0700 -o axis -g axis "$data/sd"
    if [ -z "$(ls -A "$data/sd")" ]; then
        cp -R --no-preserve=ownership,mode "$seed"/. "$data/sd"/
        chown -R axis:axis "$data/sd"
        find "$data/sd" -type f -exec chmod 0600 {} +
        find "$data/sd" -type d -exec chmod 0700 {} +
        echo "seed: copied $seed into $data/sd"
    else
        echo "seed: $data/sd already has files, so the seed was not copied"
    fi
fi

# 7. The unit.
install -m 0644 -o root -g root "$here/axis.service" /etc/systemd/system/axis.service
systemctl daemon-reload
echo "unit: installed /etc/systemd/system/axis.service"
systemctl enable axis.service
echo "unit: enabled (starts at boot)"

echo
echo "Done. Nothing was started. Before starting it, stop anything else using the modem"
echo "(see README.md: the old v3bridge.py bridge). Then:"
echo "  sudo systemctl start axis"
echo "  journalctl -u axis -f"
