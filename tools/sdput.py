# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Copy a folder onto the board's storage over USB serial, checking every file's byte sum.

usage: python -I tools/sdput.py COM18 LOCAL_FOLDER /doors/lord [--baud=921600]

Uses the firmware's @MKDIR/@LS/@PUT commands (src/platform/cyd/serialfiles.h). From Git Bash,
prefix MSYS_NO_PATHCONV=1 so the board path isn't rewritten into a Windows path.
"""
import os
import sys
import time

import serial


def answer(s, timeout=10):
    s.timeout = timeout
    while True:
        line = s.readline()
        if not line:
            raise RuntimeError("no answer from the board")
        line = line.decode("ascii", "replace").strip()
        if line.startswith("@"):
            return line


def listing(s, remote):
    s.write(f"@LS {remote}\n".encode())
    have = {}
    while True:
        line = answer_any(s)
        if line == "@END":
            return have
        name, _, size = line.rpartition(" ")
        if name and not size.endswith("/") and size.isdigit() and not line.startswith("axis:"):
            have[name] = int(size)  # (log lines from the board can land in the middle of a listing)


def answer_any(s, timeout=10):
    s.timeout = timeout
    line = s.readline()
    if not line:
        raise RuntimeError("no answer from the board")
    return line.decode("ascii", "replace").strip()


def main(port, local, remote, baud=115200):
    remote = remote.rstrip("/")
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 2
    s.dtr = s.rts = False  # set before opening, or opening resets the board
    s.open()
    time.sleep(0.3)
    s.reset_input_buffer()
    if baud != 115200:  # the firmware's @BAUD: faster for big files, back to 115200 at the end
        s.write(f"\n@BAUD {baud}\n".encode())
        answer(s)
        s.flush()
        s.baudrate = baud
        time.sleep(0.2)
        s.reset_input_buffer()
    s.write(f"\n@MKDIR {remote}\n".encode())
    if answer(s) != "@OK":
        sys.exit("board did not make the folder")
    files = sorted(n for n in os.listdir(local) if os.path.isfile(os.path.join(local, n)))
    t0, sent = time.time(), 0
    for i, name in enumerate(files):
        data = open(os.path.join(local, name), "rb").read()
        for attempt in range(3):
            try:
                s.write(f"@PUT {remote}/{name} {len(data)}\n".encode())
                r = answer(s)
                if r != "@READY":
                    raise RuntimeError(r)
                for off in range(0, len(data), 512):
                    s.write(data[off:off + 512])
                    if answer(s) != "@K":
                        raise RuntimeError("no ack")
                r = answer(s)
                if r != f"@DONE {sum(data) & 0xFFFFFFFF}":
                    raise RuntimeError(r)
                break
            except RuntimeError as e:
                print("retry", name, e)
                time.sleep(1)
                s.reset_input_buffer()
        else:
            sys.exit(f"failed: {name}")
        sent += len(data)
        print(f"[{i + 1}/{len(files)}] {name} {len(data)}  {sent / max(time.time() - t0, 0.01) / 1024:.0f} KB/s", flush=True)
    have = listing(s, remote)
    bad = [n for n in files if have.get(n) != os.path.getsize(os.path.join(local, n))]
    print(f"done: {sent // 1024} KB in {int(time.time() - t0)} s; board listing {'matches' if not bad else 'DIFFERS: ' + ', '.join(bad)}")
    if baud != 115200:
        s.write(b"@BAUD 115200\n")
        time.sleep(0.3)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--baud=")]
    rate = next((int(a.split("=")[1]) for a in sys.argv[1:] if a.startswith("--baud=")), 115200)
    if len(args) != 3:
        sys.exit(__doc__)
    main(args[0], args[1], args[2], rate)
