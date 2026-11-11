# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Load the BBS board with busy callers while recording its free heap.

  python -I tools/busytest.py --main 192.168.1.164 --follower 192.168.1.165 \
      --port COM20 --follower-port COM21 --minutes 15 --out busy.csv

Callers (each a thread with a telnet socket, logged in the way tools/seven_login.py does):
  A (main)      Doors > LORD, forest fights in a loop, back to town
  B (main)      Doors > ANSI Packs, opens each pack in turn and plays a piece
  C (main)      message areas, then file areas, then one ZMODEM download (received and discarded)
  D (follower)  moves between menus, so the main serves theme screens through T

Events: at 3 minutes the follower is reset (@RESTART force on --follower-port), at 5 minutes
the main is told to check DNS now (@DDNS now on --port).

Every 30 s @STATUS is polled on --port (DTR and RTS held low, like tools/sdput.py). One CSV row
per poll: t,free,largest,wmin,wlarge,wdoing,resets. Ends with PASS or FAIL and the lowest
numbers seen. FAIL on: a reset (up= goes down), a line with ALLOC FAILED, wmin < 61440,
wlarge < 32768.

Passwords come from local/test-users.txt (handle|location|password). They are never printed
and never put on a command line. RevJMoney is skipped. Needs four callers in that file.
"""
from __future__ import annotations

import argparse
import os
import re
import socket
import sys
import threading
import time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

WMIN_FLOOR = 61440
WLARGE_FLOOR = 32768
POLL_S = 30
RESTART_AT_S = 3 * 60
DDNS_AT_S = 5 * 60

CSV_HEAD = "t,free,largest,wmin,wlarge,wdoing,resets"


def say(line):
    print(line, flush=True)


# --- login: reuse tools/seven_login.py -------------------------------------

def load_login():
    """(Call class, load_users function) from seven_login.py, or None when it cannot load."""
    try:
        import seven_login
    except Exception as e:  # pragma: no cover
        say("cannot load tools/seven_login.py: %s" % e)
        return None
    return seven_login.Call, seven_login.load_users


class Stop(Exception):
    pass


class Worker(threading.Thread):
    """One caller. run_caller() loops until the shared stop event is set; failures reconnect."""

    def __init__(self, name, host, handle, password, call_cls, stop, ledger):
        super().__init__(name=name, daemon=True)
        self.host = host
        self.handle = handle
        self.password = password
        self.call_cls = call_cls
        self.stop = stop
        self.ledger = ledger
        self.call = None
        self.rounds = 0

    # helpers -----------------------------------------------------------
    def check(self):
        if self.stop.is_set():
            raise Stop()

    def wait(self, pattern, seconds=15):
        """Pump until pattern shows in what arrived since the last mark. False on timeout."""
        self.check()
        start = len(self.call.app)
        end = time.time() + seconds
        while time.time() < end:
            self.check()
            if not self.call.pump(0.5):
                if self.call.sock is None or not self._alive():
                    raise ConnectionError("dropped")
            fresh = self.call.app[start:].decode("cp437", "replace")
            fresh = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", fresh)
            if re.search(pattern, fresh, re.I):
                return fresh
        return ""

    def _alive(self):
        try:
            self.call.sock.settimeout(0.0)
            self.call.sock.send(b"")
            return True
        except OSError:
            return False

    def keys(self, s, settle=0.4):
        self.check()
        self.call.send(s)
        self.call.pump(settle)

    def screen(self):
        return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", self.call.text())

    def to_menu(self):
        """Back to the main menu: Esc and q a few times until Command shows."""
        for _ in range(6):
            self.call.app.clear()
            self.keys("\x1b", 0.6)
            if re.search(r"Command|Main Menu", self.screen(), re.I):
                return True
            self.keys("q", 0.8)
            if re.search(r"Command|Main Menu", self.screen(), re.I):
                return True
        return False

    def door_key(self, name_re):
        """Open the Doors window and return the key of the row matching name_re, or None."""
        self.call.app.clear()
        self.keys("d", 1.5)
        text = self.screen()
        m = re.search(r"\[(.)\]\s*(?:%s)" % name_re, text, re.I)
        return m.group(1) if m else None

    # lifecycle ---------------------------------------------------------
    def run(self):
        while not self.stop.is_set():
            self.call = self.call_cls(self.host, self.handle, self.password)
            try:
                self.call.connect()
                self.call.login()
                while not self.stop.is_set():
                    self.run_caller()
                    self.rounds += 1
            except Stop:
                pass
            except Exception as e:
                self.ledger.note("%s: %s (reconnecting)" % (self.name, str(e)[:80]))
                self.stop.wait(8)
            finally:
                self.call.close()

    def run_caller(self):
        raise NotImplementedError


class CallerA(Worker):
    """LORD forest fights."""

    def run_caller(self):
        if not self.to_menu():
            raise ConnectionError("no main menu")
        key = self.door_key(r"LORD")
        if not key:
            raise RuntimeError("no LORD row in Doors")
        self.keys(key, 1.0)
        self.wait(r"LORD|Legend|Red Dragon|town|\(press|key", 40)
        for _ in range(4):
            self.keys("\r", 1.5)
            if re.search(r"Town Square|Forest|Fight|\bF\b", self.screen(), re.I):
                break
        for _ in range(12):  # forest fights
            self.keys("f", 1.5)
            for _ in range(14):  # attack until the fight ends (the keys are harmless in town)
                self.keys("a", 0.7)
                if re.search(r"slain|defeated|you have won|you run|dead|Town Square", self.screen()[-600:], re.I):
                    break
            self.keys("\r", 0.8)
            self.keys("r", 0.8)  # run, in case a fight is still going
            self.keys("\r", 0.8)
        self.keys("q", 1.5)  # back to town, then out of the game
        self.keys("y\r", 1.5)
        self.wait(r"Command|Main Menu|Doors", 20)


class CallerB(Worker):
    """ANSI packs: open each pack in turn, play a piece."""

    def run_caller(self):
        if not self.to_menu():
            raise ConnectionError("no main menu")
        key = self.door_key(r"ANSI Packs|ANSI PACK VIEWER|ANSI")
        if not key:
            raise RuntimeError("no ANSI Packs row in Doors")
        self.keys(key, 1.5)
        listing = self.screen()
        packs = sorted(set(re.findall(r"\[(\w)\]", listing)))[:12] or ["1", "2", "3"]
        for p in packs:
            self.keys(p, 1.5)  # open the pack
            self.keys("\r", 2.5)  # play a piece
            self.keys(" ", 2.5)
            self.keys("\x1b", 1.0)  # back to the pack list
        self.to_menu()


class CallerC(Worker):
    """Message areas, file areas, one ZMODEM download."""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.xfer = find_xfertest()

    def run_caller(self):
        if not self.to_menu():
            raise ConnectionError("no main menu")
        self.keys("m", 1.5)
        for _ in range(3):  # walk a few message areas
            self.keys("\r", 1.5)
            self.keys("n", 1.0)
        self.to_menu()
        self.call.app.clear()
        self.keys("f", 1.5)
        self.wait(r"Area number|Select|Area", 8)
        for area in ("1", "2", "3"):
            self.call.app.clear()
            self.keys(area + "\r", 1.5)
            files = re.findall(r"\b([A-Za-z0-9_~-]{1,8}\.[A-Za-z0-9]{1,3})\b", self.screen())
            if files and self.download(files[0]):
                break
            self.keys("q", 1.0)
        self.to_menu()

    def download(self, name):
        self.keys("d" + name + "\r", 1.5)
        if not self.wait(r"MODEM|protocol", 8):
            return False
        self.keys("z", 0.5)
        if not self.wait(r"download now|start your|ZMODEM", 10):
            return False
        ok = zmodem_discard(self.call, self.stop, 90)
        self.call.pump(2)
        return ok


class CallerD(Worker):
    """Follower: move between menus so the main serves theme screens."""

    def run_caller(self):
        if not self.to_menu():
            raise ConnectionError("no main menu")
        for k in ("m", "f", "d", "w", "u"):
            self.keys(k, 2.0)
            self.call.app.clear()
            self.keys("\x1b", 1.5)
            self.keys("q", 1.0) if not re.search(r"Command|Main Menu", self.screen(), re.I) else None
        self.stop.wait(2)


# --- ZMODEM receive, bytes discarded ---------------------------------------

ZDLE = 0x18
ZPAD = 0x2A


def crc16(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def hex_header(ftype, p0=0, p1=0, p2=0, p3=0):
    body = bytes((ftype, p0, p1, p2, p3))
    c = crc16(body)
    h = (body + bytes((c >> 8, c & 255))).hex()
    tail = b"\r\x8a" + (b"" if ftype in (3, 8) else b"\x11")  # no XON after ZACK/ZFIN
    return b"**" + bytes((ZDLE,)) + b"B" + h.encode() + tail


def find_xfertest():
    for rel in ("build/xfertest.exe", "build/xfertest", "build/tools/xfertest/xfertest.exe"):
        p = os.path.join(ROOT, rel)
        if os.path.isfile(p):
            return p
    return ""


def zmodem_discard(call, stop, seconds):
    """Receive a ZMODEM file and throw the bytes away. A small receiver: answers ZRQINIT with
    ZRINIT, ZFILE with ZRPOS 0, data subpackets that want it with ZACK, ZEOF with ZRINIT and
    ZFIN with ZFIN plus OO. Returns True when the sender said ZFIN."""
    ZRQINIT, ZRINIT, ZFILE, ZFIN, ZRPOS, ZDATA, ZEOF, ZACK = 0, 1, 4, 8, 9, 10, 11, 3
    sock = call.sock
    sock.settimeout(0.3)
    end = time.time() + seconds
    sock.sendall(hex_header(ZRINIT, 0, 0, 0, 0x23))  # CANFDX | CANOVIO | CANFC32
    buf = bytearray()
    pos = 0
    in_data = False
    while time.time() < end and not stop.is_set():
        try:
            data = sock.recv(4096)
        except socket.timeout:
            sock.sendall(hex_header(ZRINIT, 0, 0, 0, 0x23)) if not in_data else None
            continue
        except OSError:
            return False
        if not data:
            return False
        out = bytearray()  # telnet: drop IAC, keep doubled 255
        i = 0
        while i < len(data):
            b = data[i]
            if b == 255 and i + 1 < len(data):
                if data[i + 1] == 255:
                    out.append(255)
                    i += 2
                    continue
                i += 3 if 251 <= data[i + 1] <= 254 else 2
                continue
            out.append(b)
            i += 1
        buf += out
        while True:
            m = re.search(rb"\*\*\x18([ABC])", bytes(buf))
            if not m:
                # keep a data stream moving: look for data end markers (ZDLE + h/i/j/k)
                if in_data:
                    for em in re.finditer(rb"\x18([hijk])", bytes(buf)):
                        pos += 1
                        if em.group(1) in (b"j", b"k"):
                            sock.sendall(hex_header(ZACK, pos & 255, (pos >> 8) & 255, (pos >> 16) & 255, (pos >> 24) & 255))
                    buf.clear()
                elif len(buf) > 4096:
                    del buf[:-8]
                break
            kind = m.group(1)
            rest = bytes(buf[m.end():])
            if kind == b"B":
                if len(rest) < 14:
                    break
                try:
                    ftype = int(rest[0:2], 16)
                except ValueError:
                    del buf[: m.end()]
                    continue
                del buf[: m.end() + 14]
            else:
                dec = bytearray()
                j = 0
                while j < len(rest) and len(dec) < 1:
                    if rest[j] == ZDLE and j + 1 < len(rest):
                        dec.append(rest[j + 1] ^ 0x40)
                        j += 2
                    else:
                        dec.append(rest[j])
                        j += 1
                if not dec:
                    break
                ftype = dec[0]
                # drop the rest of the header (4 position bytes + crc) as best we can
                del buf[: m.end() + min(len(rest), j + 9)]
            if ftype == ZRQINIT:
                sock.sendall(hex_header(ZRINIT, 0, 0, 0, 0x23))
            elif ftype == ZFILE:
                sock.sendall(hex_header(ZRPOS, 0, 0, 0, 0))
                pos = 0
                in_data = True
            elif ftype == ZDATA:
                in_data = True
            elif ftype == ZEOF:
                in_data = False
                sock.sendall(hex_header(ZRINIT, 0, 0, 0, 0x23))
            elif ftype == ZFIN:
                sock.sendall(hex_header(ZFIN) + b"OO")
                return True
    return False


# --- serial ----------------------------------------------------------------

def open_serial(port):
    import serial
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 2
    s.dtr = s.rts = False  # set before opening, or opening resets the board
    s.open()
    time.sleep(0.3)
    s.reset_input_buffer()
    return s


def parse_status(line):
    """key=value tokens of an @STATUS line into a dict (strings)."""
    out = {}
    for tok in line.split():
        k, eq, v = tok.partition("=")
        if eq:
            out[k] = v
    return out


def to_int(v, default=0):
    try:
        return int(v)
    except (TypeError, ValueError):
        return default


class Ledger:
    def __init__(self):
        self.lock = threading.Lock()
        self.notes = []

    def note(self, s):
        with self.lock:
            self.notes.append(s)
        say("note: " + s)


class Monitor:
    """Polls @STATUS and reads every other serial line for ALLOC FAILED."""

    def __init__(self, ser):
        self.ser = ser
        self.alloc_failed = 0
        self.last_up = None
        self.resets = 0

    def poll(self):
        self.ser.write(b"\n@STATUS\n")
        self.ser.flush()
        end = time.time() + 6
        found = None
        while time.time() < end:
            raw = self.ser.readline()
            if not raw:
                continue
            line = raw.decode("ascii", "replace").strip()
            if "ALLOC FAILED" in line:
                self.alloc_failed += 1
            if "up=" in line and "heap=" in line and found is None:
                found = parse_status(line[line.index("id=") :] if "id=" in line else line)
                if self.last_up is not None and to_int(found.get("up")) < self.last_up:
                    self.resets += 1
                self.last_up = to_int(found.get("up"))
                break
        self.drain()
        return found

    def drain(self):
        try:
            while self.ser.in_waiting:
                line = self.ser.readline().decode("ascii", "replace")
                if "ALLOC FAILED" in line:
                    self.alloc_failed += 1
        except OSError:
            pass


def serial_send(port, cmd):
    s = open_serial(port)
    try:
        s.write(("\n" + cmd + "\n").encode())
        s.flush()
        time.sleep(0.5)
    finally:
        s.close()


# --- main ------------------------------------------------------------------

def parse_args(argv):
    ap = argparse.ArgumentParser(description="Busy-callers memory test for the BBS board.")
    ap.add_argument("--main", required=True, help="main board IP")
    ap.add_argument("--follower", required=True, help="follower board IP")
    ap.add_argument("--port", required=True, help="serial port of the main (status and @DDNS now)")
    ap.add_argument("--follower-port", default="", help="serial port of the follower (@RESTART force at 3 min)")
    ap.add_argument("--minutes", type=float, default=15.0)
    ap.add_argument("--out", required=True, help="CSV file to write")
    return ap.parse_args(argv)


def selftest():
    st = parse_status("id=a role=main heap=100 largest=50 up=7 wmin=9 wdoing=lord_x")
    assert st["heap"] == "100" and st["wdoing"] == "lord_x"
    h = hex_header(1, 0, 0, 0, 0x23)
    assert h.startswith(b"**\x18B01") and h.endswith(b"\r\x8a\x11")
    assert crc16(b"123456789") == 0x31C3
    say("selftest ok")
    return 0


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]
    if argv == ["--selftest"]:
        return selftest()
    args = parse_args(argv)
    login = load_login()
    if login is None:
        return 2
    call_cls, load_users = login
    users = load_users(os.path.join(ROOT, "local", "test-users.txt"))
    if len(users) < 4:
        say("Not running. local/test-users.txt needs 4 callers with a password (handle|location|password).")
        return 2
    stop = threading.Event()
    ledger = Ledger()
    plan = [("A", CallerA, args.main), ("B", CallerB, args.main), ("C", CallerC, args.main), ("D", CallerD, args.follower)]
    workers = []
    try:
        ser = open_serial(args.port)
    except Exception as e:
        say("cannot open the main's serial port: %s" % e)
        return 2
    mon = Monitor(ser)
    start = time.time()
    for i, (name, cls, host) in enumerate(plan):  # the main only takes two sessions itself: stagger
        handle, pw = users[i]
        w = cls("caller-" + name, host, handle, pw, call_cls, stop, ledger)
        workers.append(w)
        w.start()
        time.sleep(8)
    total = args.minutes * 60
    did_restart = did_ddns = False
    low = {"free": None, "largest": None, "wmin": None, "wlarge": None}
    reasons = []
    with open(args.out, "w", encoding="ascii", newline="") as f:
        f.write(CSV_HEAD + "\n")
        next_poll = start
        while True:
            now = time.time() - start
            if now >= total:
                break
            if not did_restart and now >= RESTART_AT_S:
                did_restart = True
                if args.follower_port:
                    try:
                        serial_send(args.follower_port, "@RESTART force")
                        ledger.note("follower reset at %ds" % now)
                    except Exception as e:
                        ledger.note("follower reset failed: %s" % e)
                else:
                    ledger.note("no --follower-port: follower not reset")
            if not did_ddns and now >= DDNS_AT_S:
                did_ddns = True
                try:
                    ser.write(b"\n@DDNS now\n")
                    ser.flush()
                    ledger.note("DNS check forced at %ds" % now)
                except Exception as e:
                    ledger.note("@DDNS now failed: %s" % e)
            if time.time() >= next_poll:
                next_poll += POLL_S
                st = mon.poll()
                if st is None:
                    ledger.note("no @STATUS answer at %ds" % now)
                else:
                    row = (int(now), to_int(st.get("heap")), to_int(st.get("largest")), to_int(st.get("wmin")),
                           to_int(st.get("wlarge")), st.get("wdoing", ""), mon.resets)
                    f.write("%d,%d,%d,%d,%d,%s,%d\n" % row)
                    f.flush()
                    for key, v in (("free", row[1]), ("largest", row[2]), ("wmin", row[3]), ("wlarge", row[4])):
                        if low[key] is None or v < low[key]:
                            low[key] = v
                    say("t=%ds free=%d largest=%d wmin=%d wlarge=%d wdoing=%s resets=%d" % row)
                    if "wmin" in st and row[3] < WMIN_FLOOR and "wmin" not in reasons:
                        reasons.append("wmin")
                    if "wlarge" in st and row[4] < WLARGE_FLOOR and "wlarge" not in reasons:
                        reasons.append("wlarge")
            mon.drain()
            time.sleep(0.5)
    stop.set()
    for w in workers:
        w.join(timeout=10)
    try:
        ser.close()
    except OSError:
        pass
    if mon.resets:
        reasons.append("reset x%d" % mon.resets)
    if mon.alloc_failed:
        reasons.append("ALLOC FAILED x%d" % mon.alloc_failed)
    verdict = "FAIL" if reasons else "PASS"
    say("%s lowest free=%s largest=%s wmin=%s wlarge=%s resets=%d alloc_failed=%d rounds=%s%s" % (
        verdict, low["free"], low["largest"], low["wmin"], low["wlarge"], mon.resets, mon.alloc_failed,
        "/".join(str(w.rounds) for w in workers), (" because " + ", ".join(reasons)) if reasons else ""))
    return 1 if reasons else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        say("stopped")
        sys.exit(1)
